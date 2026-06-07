require("dotenv").config();
const express  = require("express");
const mongoose = require("mongoose");

const MONGO_URI = "mongodb+srv://dfrancoisaudace:devDei19!@cluster0.k3hkuuq.mongodb.net/gsm_tokens?retryWrites=true&w=majority&appName=Cluster0";

const app = express();

app.use((req, res, next) => {
  res.header("Access-Control-Allow-Origin", "*");
  res.header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
  res.header("Access-Control-Allow-Headers", "*");
  if (req.method === "OPTIONS") return res.sendStatus(200);
  next();
});

app.use(express.json());

const cardSchema = new mongoose.Schema({
  uid:        { type: String, required: true, unique: true, uppercase: true },
  owner:      { type: String, default: "Unknown" },
  tokens:     { type: Number, default: 0 },
  lastAccess: { type: Date,   default: null },
  status:     { type: String, default: "idle" },
});
const Card = mongoose.model("Card", cardSchema);

app.get("/api/cards", async (req, res) => {
  try {
    const cards = await Card.find();
    res.json(cards);
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

app.get("/api/cards/:uid", async (req, res) => {
  try {
    const uid = req.params.uid.toUpperCase().replace(/:/g, "");
    const card = await Card.findOne({ uid });
    if (!card) return res.status(404).json({ success: false, reason: "Not found" });
    console.log(`[GET] ${uid} → tokens: ${card.tokens}`);
    res.json({ success: true, uid: card.uid, tokens: card.tokens, status: card.status, owner: card.owner });
  } catch (err) {
    res.status(500).json({ success: false, error: err.message });
  }
});

app.get("/api/cards/:uid/set", async (req, res) => {
  try {
    const uid = req.params.uid.toUpperCase().replace(/:/g, "");
    const tokens = parseInt(req.query.tokens);
    if (isNaN(tokens)) return res.status(400).json({ success: false, reason: "tokens required" });
    const card = await Card.findOneAndUpdate(
      { uid },
      { tokens, lastAccess: new Date(), status: tokens > 0 ? "idle" : "empty" },
      { new: true }
    );
    if (!card) return res.status(404).json({ success: false, reason: "Card not found" });
    console.log(`[SET] ${uid} → ${tokens} tokens`);
    res.json({ success: true, tokens: card.tokens });
  } catch (err) {
    res.status(500).json({ success: false, error: err.message });
  }
});

app.post("/api/cards/:uid/set", async (req, res) => {
  try {
    const uid = req.params.uid.toUpperCase().replace(/:/g, "");
    const { tokens } = req.body;
    if (tokens === undefined) return res.status(400).json({ success: false, reason: "tokens required" });
    const card = await Card.findOneAndUpdate(
      { uid },
      { tokens: parseInt(tokens), lastAccess: new Date(), status: parseInt(tokens) > 0 ? "idle" : "empty" },
      { new: true }
    );
    if (!card) return res.status(404).json({ success: false, reason: "Card not found" });
    console.log(`[SET] ${uid} → ${tokens} tokens`);
    res.json({ success: true, tokens: card.tokens });
  } catch (err) {
    res.status(500).json({ success: false, error: err.message });
  }
});

app.post("/api/cards/:uid/topup", async (req, res) => {
  try {
    const uid = req.params.uid.toUpperCase().replace(/:/g, "");
    const { amount } = req.body;
    const card = await Card.findOneAndUpdate(
      { uid },
      { $inc: { tokens: parseInt(amount) || 1 }, status: "idle" },
      { new: true }
    );
    if (!card) return res.status(404).json({ success: false, error: "Not found" });
    console.log(`[TOPUP] ${uid} → ${card.tokens} tokens`);
    res.json({ success: true, tokens: card.tokens });
  } catch (err) {
    res.status(500).json({ success: false, error: err.message });
  }
});

app.post("/api/cards", async (req, res) => {
  try {
    const { uid, owner, tokens } = req.body;
    const card = await Card.findOneAndUpdate(
      { uid: uid.toUpperCase().replace(/:/g, "") },
      { owner, tokens: tokens || 0, status: "idle" },
      { upsert: true, new: true }
    );
    console.log(`[REG] ${card.uid} — ${owner} — ${card.tokens} tokens`);
    res.json({ success: true, card });
  } catch (err) {
    res.status(500).json({ success: false, error: err.message });
  }
});

app.delete("/api/cards/:uid", async (req, res) => {
  try {
    const uid = req.params.uid.toUpperCase().replace(/:/g, "");
    await Card.deleteOne({ uid });
    console.log(`[DELETE] ${uid}`);
    res.json({ success: true });
  } catch (err) {
    res.status(500).json({ success: false, error: err.message });
  }
});

app.get("/api/cards/:uid/status", async (req, res) => {
  try {
    const uid = req.params.uid.toUpperCase().replace(/:/g, "");
    const online = req.query.online === "true";
    const card = await Card.findOneAndUpdate(
      { uid },
      { status: online ? "active" : "idle" },
      { new: true }
    );
    if (!card) return res.status(404).json({ success: false });
    console.log(`[STATUS] ${uid} → ${online ? "ONLINE" : "OFFLINE"}`);
    res.json({ success: true, status: card.status });
  } catch (err) {
    res.status(500).json({ success: false, error: err.message });
  }
});

const PORT = process.env.PORT || 3000;
mongoose.connect(MONGO_URI).then(() => {
  app.listen(PORT, () => {
    console.log("\n[API] Server running on port " + PORT);
    console.log("[API] Endpoints:");
    console.log("  GET    /api/cards");
    console.log("  GET    /api/cards/:uid");
    console.log("  GET    /api/cards/:uid/set?tokens=N");
    console.log("  GET    /api/cards/:uid/status?online=true");
    console.log("  POST   /api/cards");
    console.log("  POST   /api/cards/:uid/topup");
    console.log("  POST   /api/cards/:uid/set");
    console.log("  DELETE /api/cgit add server.jsards/:uid\n");
  });
}).catch(err => console.error("[DB]", err));