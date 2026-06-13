require("dotenv").config();
const mongoose = require("mongoose");

const MONGO_URI = "mongodb+srv://dfrancoisaudace:devDei19!@cluster0.k3hkuuq.mongodb.net/gsm_tokens?retryWrites=true&w=majority&appName=Cluster0";

const cardSchema = new mongoose.Schema({
  uid:        { type: String, required: true, unique: true, uppercase: true },
  owner:      { type: String, default: "Unknown" },
  tokens:     { type: Number, default: 0 },
  lastAccess: { type: Date,   default: null },
  status:     { type: String, default: "idle" },
});
const Card = mongoose.model("Card", cardSchema);

async function main() {
  await mongoose.connect(MONGO_URI);
  console.log("[DB] Connected");
  const card = await Card.findOneAndUpdate(
    { uid: "32474344" },
    { uid: "32474344", owner: "RUGIRA pool card", tokens: 10, status: "idle" },
    { upsert: true, new: true }
  );
  console.log("Card registered:", card.uid, "| Tokens:", card.tokens);
  process.exit(0);
}
main().catch(err => { console.error(err); process.exit(1); });
