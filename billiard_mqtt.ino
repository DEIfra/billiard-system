/*
 * ============================================================
 *  BILLIARD POOL — ESP32 + SIM800C + RC522 + MQTT
 * ============================================================
 *  Wiring:
 *   RC522 SDA  → GPIO 5   | SIM800C TX → GPIO 25
 *   RC522 RST  → GPIO 22  | SIM800C RX → GPIO 26
 *   RC522 SCK  → GPIO 18  | Servo      → GPIO 13
 *   RC522 MOSI → GPIO 23  | Green LED  → GPIO 2
 *   RC522 MISO → GPIO 19  | Red LED    → GPIO 15
 *
 *  MQTT broker: HiveMQ Cloud
 *  Topics:
 *   PUBLISH  → pool/scan          { uid, tableId }
 *   SUBSCRIBE← pool/access/result { granted, tokens, reason }
 * ============================================================
 */

#include <SPI.h>
#include <MFRC522.h>
#include <ESP32Servo.h>
#include <HardwareSerial.h>

// ── Pins ──────────────────────────────────────────────────────
#define RFID_SS   5
#define RFID_RST  22
#define GSM_RX    25
#define GSM_TX    26
#define SERVO_PIN 13
#define LED_GREEN 2
#define LED_RED   15

// ── Config ────────────────────────────────────────────────────
const char* APN         = "internet";       // ← your SIM APN
const char* APN_USER    = "";
const char* APN_PASS    = "";

// HiveMQ Cloud
const char* MQTT_HOST   = "43f1ecd47dea4aef897d2326b80331a5.s1.eu.hivemq.cloud";
const int   MQTT_PORT   = 8883;             // TLS
const char* MQTT_USER   = "DEI_I";
const char* MQTT_PASS   = "devDei19!";
const char* TABLE_ID    = "table_rugira";   // unique ID for this table

// Topics
const char* TOPIC_SCAN   = "pool/scan";
const char* TOPIC_RESULT = "pool/access/result";

#define SERVO_OPEN     90
#define SERVO_CLOSED   0
#define OPEN_DURATION  10000
#define GSM_BAUD       9600

// ── Objects ───────────────────────────────────────────────────
MFRC522        rfid(RFID_SS, RFID_RST);
Servo          tableServo;
HardwareSerial gsmSerial(2);

bool gprsReady  = false;
bool mqttConn   = false;
String lastResult = "";

// ── AT helpers ────────────────────────────────────────────────
String sendAT(const char* cmd, uint32_t ms = 2000) {
  while (gsmSerial.available()) gsmSerial.read();
  Serial.print("[AT] >> "); Serial.println(cmd);
  gsmSerial.println(cmd);
  String r = ""; uint32_t t = millis();
  while (millis() - t < ms) {
    while (gsmSerial.available()) r += (char)gsmSerial.read();
    if (r.indexOf("OK")    != -1 || r.indexOf("ERROR") != -1 ||
        r.indexOf("SEND OK")!=-1 || r.indexOf(">")     != -1) break;
  }
  r.trim();
  if (r.length()) { Serial.print("[AT] << "); Serial.println(r); }
  return r;
}

bool waitFor(const char* tok, uint32_t ms = 15000) {
  String buf=""; uint32_t t=millis();
  while (millis()-t < ms) {
    while (gsmSerial.available()) buf += (char)gsmSerial.read();
    if (buf.indexOf(tok) != -1) { Serial.println(buf); return true; }
  }
  return false;
}

// ── GSM init ──────────────────────────────────────────────────
bool initModem() {
  for (int i=0;i<10;i++) {
    if (sendAT("AT",1000).indexOf("OK")!=-1) {
      sendAT("ATE0"); sendAT("AT+CMEE=2");
      return true;
    }
    delay(1000);
  }
  return false;
}

bool waitForNetwork() {
  for (int i=0;i<30;i++) {
    String r=sendAT("AT+CREG?",1000);
    if (r.indexOf(",1")!=-1||r.indexOf(",5")!=-1) {
      sendAT("AT+CSQ"); return true;
    }
    delay(2000);
  }
  return false;
}

bool openGPRS() {
  sendAT("AT+CIPSHUT",3000);
  sendAT("AT+SAPBR=0,1",3000);
  sendAT("AT+SAPBR=3,1,\"Contype\",\"GPRS\"");
  char b[80];
  snprintf(b,80,"AT+SAPBR=3,1,\"APN\",\"%s\"",APN);      sendAT(b);
  snprintf(b,80,"AT+SAPBR=3,1,\"USER\",\"%s\"",APN_USER); sendAT(b);
  snprintf(b,80,"AT+SAPBR=3,1,\"PWD\",\"%s\"",APN_PASS);  sendAT(b);
  if (sendAT("AT+SAPBR=1,1",10000).indexOf("ERROR")!=-1) return false;
  delay(2000);
  String ip=sendAT("AT+SAPBR=2,1",3000);
  return ip.indexOf("0.0.0.0")==-1;
}

// ── MQTT over TCP (SIM800C AT commands) ───────────────────────
// SIM800C supports MQTT via AT+CIPSTART (raw TCP) + manual packet building
// We use the simpler MQTTS approach via SSL TCP

bool mqttConnect() {
  Serial.println("[MQTT] Connecting to HiveMQ...");

  // Open SSL TCP connection
  sendAT("AT+CIPSSL=1", 2000);   // enable SSL
  sendAT("AT+CIPMUX=0", 2000);   // single connection

  char cmd[120];
  snprintf(cmd,120,"AT+CIPSTART=\"TCP\",\"%s\",%d", MQTT_HOST, MQTT_PORT);
  String r = sendAT(cmd, 10000);
  if (!waitFor("CONNECT", 15000)) {
    Serial.println("[MQTT] TCP connect failed");
    return false;
  }

  // Build MQTT CONNECT packet manually
  // Fixed header
  String clientId = "ESP32_RUGIRA";
  String payload = "";

  // Protocol Name "MQTT"
  payload += (char)0x00; payload += (char)0x04;
  payload += "MQTT";
  payload += (char)0x04;   // Protocol Level 3.1.1
  payload += (char)0xC2;   // Connect flags: username+password+clean session
  payload += (char)0x00; payload += (char)0x3C; // Keep alive 60s

  // Client ID
  payload += (char)(clientId.length()>>8);
  payload += (char)(clientId.length()&0xFF);
  payload += clientId;

  // Username
  String u = MQTT_USER;
  payload += (char)(u.length()>>8); payload += (char)(u.length()&0xFF);
  payload += u;

  // Password
  String p = MQTT_PASS;
  payload += (char)(p.length()>>8); payload += (char)(p.length()&0xFF);
  payload += p;

  // Fixed header byte 1 = 0x10 (CONNECT), remaining length
  int remLen = payload.length();
  String packet = "";
  packet += (char)0x10;
  packet += (char)remLen;
  packet += payload;

  // Send via AT+CIPSEND
  char sendCmd[20];
  snprintf(sendCmd,20,"AT+CIPSEND=%d", packet.length());
  sendAT(sendCmd, 2000);
  delay(100);
  gsmSerial.print(packet);

  if (!waitFor("CONNACK",5000) && !waitFor("SEND OK",5000)) {
    Serial.println("[MQTT] CONNACK not received — trying response check");
  }

  Serial.println("[MQTT] Connected to HiveMQ Cloud");
  mqttConn = true;

  // Subscribe to result topic
  mqttSubscribe(TOPIC_RESULT);
  return true;
}

void mqttSubscribe(const char* topic) {
  String t = topic;
  String packet = "";
  packet += (char)0x82; // SUBSCRIBE fixed header
  int remLen = 2 + 2 + t.length() + 1;
  packet += (char)remLen;
  packet += (char)0x00; packet += (char)0x01; // Packet ID
  packet += (char)(t.length()>>8); packet += (char)(t.length()&0xFF);
  packet += t;
  packet += (char)0x01; // QoS 1

  char cmd[20]; snprintf(cmd,20,"AT+CIPSEND=%d",packet.length());
  sendAT(cmd,2000); delay(100);
  gsmSerial.print(packet);
  delay(500);
  Serial.println("[MQTT] Subscribed to " + t);
}

void mqttPublish(const char* topic, const String& payload) {
  String t = topic;
  String packet = "";
  packet += (char)0x30; // PUBLISH, no QoS
  int remLen = 2 + t.length() + payload.length();
  packet += (char)remLen;
  packet += (char)(t.length()>>8); packet += (char)(t.length()&0xFF);
  packet += t;
  packet += payload;

  char cmd[20]; snprintf(cmd,20,"AT+CIPSEND=%d",packet.length());
  sendAT(cmd,2000); delay(100);
  gsmSerial.print(packet);
  delay(300);
  Serial.println("[MQTT] Published to " + t + ": " + payload);
}

void mqttPing() {
  // PINGREQ
  char ping[2] = {(char)0xC0, (char)0x00};
  sendAT("AT+CIPSEND=2",1000);
  delay(100);
  gsmSerial.write((uint8_t*)ping, 2);
}

// ── Check for incoming MQTT messages ──────────────────────────
void checkMQTT() {
  String raw = "";
  while (gsmSerial.available()) raw += (char)gsmSerial.read();
  if (raw.length() == 0) return;

  // Look for PUBLISH packet (0x30) containing our topic
  if (raw.indexOf(TOPIC_RESULT) != -1) {
    // Extract JSON — find first { to last }
    int start = raw.indexOf('{');
    int end   = raw.lastIndexOf('}');
    if (start != -1 && end != -1 && end > start) {
      lastResult = raw.substring(start, end+1);
      Serial.println("[MQTT] Received: " + lastResult);
      handleResult(lastResult);
    }
  }
}

// ── Handle access result ──────────────────────────────────────
void handleResult(const String& json) {
  bool granted = json.indexOf("\"granted\":true")  != -1 ||
                 json.indexOf("\"granted\": true") != -1;
  if (granted) {
    Serial.println("[ACCESS] GRANTED — opening table");
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_RED,   LOW);
    tableServo.write(SERVO_OPEN);
    delay(OPEN_DURATION);
    tableServo.write(SERVO_CLOSED);
    digitalWrite(LED_GREEN, LOW);
    Serial.println("[ACCESS] Table locked");
  } else {
    Serial.println("[ACCESS] DENIED");
    for (int i=0;i<3;i++) {
      digitalWrite(LED_RED, HIGH); delay(200);
      digitalWrite(LED_RED, LOW);  delay(200);
    }
  }
}

// ── RFID ──────────────────────────────────────────────────────
String getUID() {
  String uid="";
  for (byte i=0;i<rfid.uid.size;i++) {
    if (rfid.uid.uidByte[i]<0x10) uid+="0";
    uid+=String(rfid.uid.uidByte[i],HEX);
    if (i<rfid.uid.size-1) uid+=":";
  }
  uid.toUpperCase();
  return uid;
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== BILLIARD POOL — MQTT MODE ===");

  pinMode(LED_GREEN, OUTPUT); pinMode(LED_RED, OUTPUT);
  digitalWrite(LED_GREEN, LOW); digitalWrite(LED_RED, LOW);

  tableServo.attach(SERVO_PIN);
  tableServo.write(SERVO_CLOSED);

  SPI.begin();
  rfid.PCD_Init();
  Serial.println("[RFID] RC522 ready");

  gsmSerial.begin(GSM_BAUD, SERIAL_8N1, GSM_RX, GSM_TX);
  delay(3000);

  if (!initModem())      { Serial.println("[FAIL] Modem");   while(1); }
  if (!waitForNetwork()) { Serial.println("[FAIL] Network"); while(1); }
  if (!openGPRS())       { Serial.println("[FAIL] GPRS");    while(1); }

  gprsReady = true;

  if (!mqttConnect())    { Serial.println("[FAIL] MQTT");    while(1); }

  // Ready — 3 green blinks
  for (int i=0;i<3;i++) {
    digitalWrite(LED_GREEN,HIGH); delay(200);
    digitalWrite(LED_GREEN,LOW);  delay(200);
  }
  Serial.println("\n[READY] Tap card 32:47:43:4D...");
}

// ── Loop ──────────────────────────────────────────────────────
uint32_t lastPing = 0;

void loop() {
  // MQTT keepalive ping every 30s
  if (millis() - lastPing > 30000) {
    mqttPing();
    lastPing = millis();
  }

  // Check for incoming MQTT messages
  checkMQTT();

  // RFID scan
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    delay(150);
    return;
  }

  String uid = getUID();
  Serial.println("\n[RFID] Scanned: " + uid);

  // Flash both LEDs while waiting for server response
  digitalWrite(LED_GREEN, HIGH); digitalWrite(LED_RED, HIGH);

  // Publish scan event to Node.js via MQTT
  String msg = "{\"uid\":\"" + uid + "\",\"tableId\":\"" + TABLE_ID + "\"}";
  mqttPublish(TOPIC_SCAN, msg);

  // Wait for result (up to 8 seconds)
  uint32_t t = millis();
  lastResult = "";
  while (lastResult == "" && millis()-t < 8000) {
    checkMQTT();
    delay(100);
  }

  if (lastResult == "") {
    Serial.println("[TIMEOUT] No response from server");
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_RED,   HIGH); delay(2000);
    digitalWrite(LED_RED,   LOW);
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  Serial.println("\n[READY] Tap card...");
}
