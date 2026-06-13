/*
 * ============================================================
 *  EEPROM TOKEN SETTER
 *  Run this sketch to manually set tokens in EEPROM
 *  Then flash billiard_final.ino — it will read these tokens
 * ============================================================
 */

#include <EEPROM.h>

#define EEPROM_SIZE  8
#define ADDR_TOKENS  0   // 4 bytes
#define ADDR_FLAG    4   // 1 byte — 0xAB = initialized

// ← SET HOW MANY TOKENS YOU WANT HERE
#define TOKENS_TO_SET  5

void setup() {
  Serial.begin(115200);
  delay(500);

  EEPROM.begin(EEPROM_SIZE);

  Serial.println("\n=== EEPROM TOKEN SETTER ===");

  // Read current value
  int current = 0;
  if (EEPROM.read(ADDR_FLAG) == 0xAB) {
    EEPROM.get(ADDR_TOKENS, current);
    Serial.print("Current tokens in EEPROM: ");
    Serial.println(current);
  } else {
    Serial.println("EEPROM not initialized yet");
  }

  // Write new value
  EEPROM.put(ADDR_TOKENS, (int)TOKENS_TO_SET);
  EEPROM.write(ADDR_FLAG, 0xAB);
  EEPROM.commit();

  // Verify
  int verify = 0;
  EEPROM.get(ADDR_TOKENS, verify);

  Serial.print("Tokens set to: ");
  Serial.println(verify);
  Serial.println("\nDone! Now flash billiard_final.ino");
  Serial.println("It will load " + String(verify) + " tokens on boot.");
}

void loop() {
  // nothing
}
