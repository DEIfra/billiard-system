#include <SPI.h>
#include <MFRC522.h>
#include <ESP32Servo.h>
#include <HardwareSerial.h>
#include <EEPROM.h>

#define RFID_SS    5
#define RFID_RST   22
#define GSM_RX     25
#define GSM_TX     26
#define SERVO_PIN  13
#define LED_GREEN  2
#define LED_RED    15
#define EEPROM_SIZE  8
#define ADDR_TOKENS  0
#define ADDR_FLAG    4

const char* APN               = "internet.com";
const char* APN_USER          = "";
const char* APN_PASS          = "";
const char* SERVER_HOST       = "billia-0o961den.b4a.run";
const char* CARD_UID          = "32:47:43:4D";
const char* CARD_UID_STRIPPED = "3247434D";

#define SERVO_OPEN    90
#define SERVO_CLOSED  0
#define OPEN_DURATION 10000
#define GSM_BAUD      9600

MFRC522        rfid(RFID_SS, RFID_RST);
Servo          tableServo;
HardwareSerial gsmSerial(2);

int  localTokens = 0;
bool gprsReady   = false;

void saveTokens(int tokens) {
  EEPROM.put(ADDR_TOKENS, tokens);
  EEPROM.write(ADDR_FLAG, 0xAB);
  EEPROM.commit();
  Serial.print("[EEPROM] Saved: "); Serial.println(tokens);
}

int loadTokens() {
  if (EEPROM.read(ADDR_FLAG) != 0xAB) return 0;
  int t; EEPROM.get(ADDR_TOKENS, t);
  Serial.print("[EEPROM] Loaded: "); Serial.println(t);
  return t;
}

String sendAT(const char* cmd, uint32_t ms = 2000) {
  while (gsmSerial.available()) gsmSerial.read();
  Serial.print("[AT] >> "); Serial.println(cmd);
  gsmSerial.println(cmd);
  String r = "";
  uint32_t t = millis();
  while (millis() - t < ms) {
    while (gsmSerial.available()) r += (char)gsmSerial.read();
    if (r.indexOf("OK") != -1 || r.indexOf("ERROR") != -1) break;
  }
  r.trim();
  if (r.length()) { Serial.print("[AT] << "); Serial.println(r); }
  return r;
}

bool initModem() {
  Serial.println("[GSM] Initialising...");
  for (int i = 0; i < 10; i++) {
    if (sendAT("AT", 1000).indexOf("OK") != -1) {
      sendAT("ATE0"); sendAT("AT+CMEE=2");
      Serial.println("[GSM] Modem OK");
      return true;
    }
    delay(1000);
  }
  return false;
}

bool waitForNetwork() {
  Serial.println("[GSM] Waiting for network...");
  for (int i = 0; i < 30; i++) {
    String r = sendAT("AT+CREG?", 1000);
    if (r.indexOf(",1") != -1 || r.indexOf(",5") != -1) {
      sendAT("AT+CSQ");
      Serial.println("[GSM] Network OK");
      return true;
    }
    Serial.print(".");
    delay(2000);
  }
  return false;
}

bool openGPRS() {
  Serial.println("[GSM] Opening GPRS...");
  sendAT("AT+CIPSHUT", 3000);
  sendAT("AT+SAPBR=0,1", 3000);
  sendAT("AT+SAPBR=3,1,\"Contype\",\"GPRS\"");
  char b[80];
  snprintf(b, 80, "AT+SAPBR=3,1,\"APN\",\"%s\"",  APN);      sendAT(b);
  snprintf(b, 80, "AT+SAPBR=3,1,\"USER\",\"%s\"", APN_USER); sendAT(b);
  snprintf(b, 80, "AT+SAPBR=3,1,\"PWD\",\"%s\"",  APN_PASS); sendAT(b);
  if (sendAT("AT+SAPBR=1,1", 10000).indexOf("ERROR") != -1) return false;
  delay(2000);
  String ip = sendAT("AT+SAPBR=2,1", 3000);
  if (ip.indexOf("0.0.0.0") != -1) return false;
  Serial.println("[GSM] GPRS ready");
  return true;
}

String httpGET(const char* url) {
  Serial.print("[GET] "); Serial.println(url);
  sendAT("AT+HTTPTERM", 1000);
  delay(1000);
  sendAT("AT+HTTPINIT");
  sendAT("AT+HTTPPARA=\"CID\",1");
  char urlCmd[256];
  snprintf(urlCmd, sizeof(urlCmd), "AT+HTTPPARA=\"URL\",\"%s\"", url);
  sendAT(urlCmd, 3000);
  sendAT("AT+HTTPPARA=\"REDIR\",1");
  // plain HTTP — no SSL

  while (gsmSerial.available()) gsmSerial.read();
  gsmSerial.println("AT+HTTPACTION=0");
  Serial.println("[AT] >> AT+HTTPACTION=0");

  String raw = "";
  uint32_t t = millis();
  while (millis() - t < 35000) {
    while (gsmSerial.available()) {
      char c = gsmSerial.read();
      raw += c;
      Serial.write(c);
    }
    if (raw.indexOf("+HTTPACTION") != -1) { delay(500); break; }
    delay(50);
  }
  Serial.println();
  Serial.println("[GET] Raw: " + raw);

  int len = 0;
  int idx = raw.indexOf("+HTTPACTION");
  if (idx != -1) {
    int c1 = raw.indexOf(',', idx);
    int c2 = raw.indexOf(',', c1 + 1);
    if (c2 != -1) {
      String ls = raw.substring(c2 + 1);
      ls.trim();
      String ns = "";
      for (char ch : ls) { if (isDigit(ch)) ns += ch; else break; }
      len = ns.toInt();
    }
  }
  Serial.print("[GET] Length: "); Serial.println(len);
  if (len <= 0) { sendAT("AT+HTTPTERM"); return ""; }

  while (gsmSerial.available()) gsmSerial.read();
  char rc[30];
  snprintf(rc, sizeof(rc), "AT+HTTPREAD=%d", len);
  gsmSerial.println(rc);
  Serial.println("[AT] >> " + String(rc));

  String body = "";
  t = millis();
  while (millis() - t < 10000) {
    while (gsmSerial.available()) body += (char)gsmSerial.read();
    if (body.indexOf("OK") != -1) break;
    delay(50);
  }
  sendAT("AT+HTTPTERM");
  Serial.println("[GET] Body: " + body);

  int s = body.indexOf('{');
  int e = body.lastIndexOf('}');
  if (s != -1 && e != -1 && e > s) return body.substring(s, e + 1);
  return body;
}

void wakeUpServer() {
  Serial.println("[WAKE] Pinging server...");
  char url[120];
  snprintf(url, sizeof(url), "http://%s/api/cards", SERVER_HOST);
  httpGET(url);
  Serial.println("[WAKE] Done");
  delay(2000);
}

int fetchTokensFromDB() {
  char url[220];
  snprintf(url, sizeof(url), "http://%s/api/cards/%s",
    SERVER_HOST, CARD_UID_STRIPPED);
  String resp = httpGET(url);
  int idx = resp.indexOf("\"tokens\":");
  if (idx != -1) {
    String ns = "";
    int i = idx + 9;
    while (i < (int)resp.length() && (isDigit(resp[i]) || resp[i] == '-')) ns += resp[i++];
    int tokens = ns.toInt();
    Serial.print("[DB] Tokens: "); Serial.println(tokens);
    return tokens;
  }
  return -1;
}

bool pushTokensToDB(int tokens) {
  char url[220];
  snprintf(url, sizeof(url), "http://%s/api/cards/%s/set?tokens=%d",
    SERVER_HOST, CARD_UID_STRIPPED, tokens);
  String resp = httpGET(url);
  bool ok = resp.indexOf("true") != -1;
  Serial.println(ok ? "[DB] Push OK" : "[DB] Push FAILED");
  return ok;
}

void updateGSMStatus(bool online) {
  char url[220];
  snprintf(url, sizeof(url), "http://%s/api/cards/%s/status?online=%s",
    SERVER_HOST, CARD_UID_STRIPPED, online ? "true" : "false");
  httpGET(url);
  Serial.println(online ? "[STATUS] GSM online" : "[STATUS] GSM offline");
}

String getUID() {
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
    if (i < rfid.uid.size - 1) uid += ":";
  }
  uid.toUpperCase();
  return uid;
}

void grantAccess() {
  Serial.println("[ACCESS] GRANTED!");
  digitalWrite(LED_GREEN, HIGH); digitalWrite(LED_RED, LOW);
  tableServo.write(SERVO_OPEN);
  delay(OPEN_DURATION);
  tableServo.write(SERVO_CLOSED);
  digitalWrite(LED_GREEN, LOW);
  Serial.println("[ACCESS] Table locked");
}

void denyAccess(const char* reason) {
  Serial.print("[ACCESS] DENIED — "); Serial.println(reason);
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_RED, HIGH); delay(200);
    digitalWrite(LED_RED, LOW);  delay(200);
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== BILLIARD POOL SYSTEM ===");
  Serial.println("Server: http://" + String(SERVER_HOST));

  EEPROM.begin(EEPROM_SIZE);
  pinMode(LED_GREEN, OUTPUT); pinMode(LED_RED, OUTPUT);
  digitalWrite(LED_GREEN, LOW); digitalWrite(LED_RED, LOW);
  tableServo.attach(SERVO_PIN);
  tableServo.write(SERVO_CLOSED);
  SPI.begin();
  rfid.PCD_Init();
  Serial.println("[RFID] RC522 ready");

  gsmSerial.begin(GSM_BAUD, SERIAL_8N1, GSM_RX, GSM_TX);
  delay(3000);

  localTokens = loadTokens();

  if (!initModem())           { gprsReady = false; Serial.println("[WARN] Modem failed"); }
  else if (!waitForNetwork()) { gprsReady = false; Serial.println("[WARN] Network failed"); }
  else if (!openGPRS())       { gprsReady = false; Serial.println("[WARN] GPRS failed"); }
  else                          gprsReady = true;

  if (gprsReady) {
    wakeUpServer();
    Serial.println("[SYNC] Fetching tokens from DB...");
    int db = fetchTokensFromDB();
    if (db >= 0) { localTokens = db; saveTokens(localTokens); }
    updateGSMStatus(true);
  }

  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_GREEN, HIGH); delay(200);
    digitalWrite(LED_GREEN, LOW);  delay(200);
  }

  Serial.println("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
  Serial.print("[READY] Tokens: "); Serial.println(localTokens);
  Serial.print("[READY] GPRS:   "); Serial.println(gprsReady ? "Online" : "Offline");
  Serial.println("[READY] Tap: " + String(CARD_UID));
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
}

void loop() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    delay(150); return;
  }

  String uid = getUID();
  Serial.println("\n[RFID] Card: " + uid);

  if (!uid.equals(CARD_UID)) {
    denyAccess("Unknown card");
    rfid.PICC_HaltA(); rfid.PCD_StopCrypto1();
    return;
  }

  if (localTokens <= 0) {
    denyAccess("No tokens — top up via dashboard");
    rfid.PICC_HaltA(); rfid.PCD_StopCrypto1();
    return;
  }

  grantAccess();
  localTokens--;
  saveTokens(localTokens);

  if (gprsReady) {
    Serial.println("[SYNC] Pushing to DB...");
    pushTokensToDB(localTokens);
  } else {
    Serial.println("[SYNC] Offline — saved locally");
  }

  rfid.PICC_HaltA(); rfid.PCD_StopCrypto1();

  Serial.print("\n[READY] Tokens left: "); Serial.println(localTokens);
  Serial.println("[READY] Tap card...\n");
}