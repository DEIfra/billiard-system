require("dotenv").config();
const mqtt     = require("mqtt");
const mongoose = require("mongoose");

const BROKER    = "mqtts://43f1ecd47dea4aef897d2326b80331a5.s1.eu.hivemq.cloud:8883";
const MQTT_USER = "DEI_I";
const MQTT_PASS = "devDei19!";

const cardSchema = new mongoose.Schema({
  uid:        { type: String, required: true, unique: true, uppercase: true },
  owner:      { type: String, default: "Unknown" },
  tokens:     { type: Number, default: 0 },
  lastAccess: { type: Date,   default: null },
  status:     { type: String, default: "idle" },
});
const Card = mongoose.model("Card", cardSchema);

async function main() {
  await mongoose.connect(process.env.MONGO_URI || "mongodb://localhost:27017/gsm_tokens");
  console.log("[DB] Connected to MongoDB");

  const client = mqtt.connect(BROKER, {
    username: MQTT_USER,
    password: MQTT_PASS,
    protocol: "mqtts",
    rejectUnauthorized: true,
  });

  client.on("connect", () => {
    console.log("[MQTT] Connected to HiveMQ Cloud");
    client.subscribe("pool/scan",  { qos: 1 });
    client.subscribe("pool/topup", { qos: 1 });
    publishState(client);
  });

  client.on("message", async (topic, payload) => {
    let msg;
    try { msg = JSON.parse(payload.toString()); }
    catch { return console.error("[MQTT] Bad JSON:", payload.toString()); }
    console.log(`[MQTT] ${topic}:`, msg);
    if (topic === "pool/scan")  await handleScan(client, msg);
    if (topic === "pool/topup") await handleTopup(client, msg);
  });

  client.on("error",     err => console.error("[MQTT] Error:", err.message));
  client.on("offline",   ()  => console.warn("[MQTT] Offline"));
  client.on("reconnect", ()  => console.log("[MQTT] Reconnecting..."));
}

async function handleScan(client, { uid, tableId }) {
  if (!uid) return;
  const normalizedUid = uid.toUpperCase().replace(/:/g, "");
  const card = await Card.findOne({ uid: normalizedUid });
  let granted = false, tokens = 0, reason = "";
  if (!card) {
    reason = "Card not registered";
  } else if (card.tokens <= 0) {
    reason = "No tokens";
  } else {
    card.tokens -= 1;
    card.lastAccess = new Date();
    card.status = "active";
    await card.save();
    granted = true; tokens = card.tokens; reason = "OK";
    setTimeout(async () => {
      const c = await Card.findOne({ uid: normalizedUid });
      if (c) { c.status = "idle"; await c.save(); }
      publishState(client);
    }, 10000);
  }
  client.publish("pool/access/result", JSON.stringify({ uid, tableId, granted, tokens, reason }), { qos: 1 });
  console.log(`[ACCESS] ${granted ? "GRANTED" : "DENIED"} — ${uid} — ${reason}`);
  publishState(client);
}

async function handleTopup(client, { uid, amount }) {
  if (!uid || !amount) return;
  const normalizedUid = uid.toUpperCase().replace(/:/g, "");
  const card = await Card.findOneAndUpdate(
    { uid: normalizedUid },
    { $inc: { tokens: parseInt(amount) } },
    { new: true }
  );
  if (card) {
    console.log(`[TOPUP] ${uid} +${amount} tokens → total: ${card.tokens}`);
    publishState(client);
  }
}

async function publishState(client) {
  const cards = await Card.find();
  client.publish("pool/state", JSON.stringify(cards), { qos: 1, retain: true });
}

main().catch(err => { console.error("[FATAL]", err); process.exit(1); });