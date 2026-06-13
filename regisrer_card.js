require("dotenv").config();
const mongoose = require("mongoose");

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
  console.log("[DB] Connected");
  const uid = "32474344";
  const card = await Card.findOneAndUpdate(
    { uid },
    { uid, owner: "RUGIRA pool card", tokens: 10, status: "idle" },
    { upsert: true, new: true }
  );
  console.log("Card registered:", card.uid, "| Tokens:", card.tokens);
  process.exit(0);
}
main().catch(err => { console.error(err); process.exit(1); });