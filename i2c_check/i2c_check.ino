// I2C check: finds out WHY the MPU6050 isn't detected.
// Upload this, open Serial Monitor at 115200, and read what it prints.
// Wiring stays the same: SDA -> D21, SCL -> D22, VCC -> 3V3 rail, GND -> GND rail.

#include <Wire.h>

void setup() {
  Serial.begin(115200);
  delay(1000);
  Wire.begin(21, 22);
}

void loop() {
  Serial.println();
  Serial.println("=== Scanning for I2C devices ===");
  int found = 0;

  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      found++;
      Serial.print("Found a device at 0x");
      Serial.println(addr, HEX);

      if (addr == 0x68 || addr == 0x69) {
        // Ask the chip what it is (WHO_AM_I register)
        Wire.beginTransmission(addr);
        Wire.write(0x75);
        Wire.endTransmission(false);
        Wire.requestFrom(addr, (byte)1);
        if (Wire.available()) {
          byte id = Wire.read();
          Serial.print("  Chip ID (WHO_AM_I) = 0x");
          Serial.println(id, HEX);
          if (id == 0x68) {
            Serial.println("  -> Real MPU6050. Wiring is GOOD.");
          } else {
            Serial.println("  -> Wiring is GOOD, but this is a different/clone chip.");
            Serial.println("     The Adafruit library rejects it. Tell Claude this chip ID.");
          }
        }
      }
    }
  }

  if (found == 0) {
    Serial.println("Nothing found.");
    Serial.println("-> The ESP32 can't reach the sensor: check power (sensor LED on?),");
    Serial.println("   SDA/SCL wires (try swapping them), and that the pins are soldered.");
  }

  delay(3000);
}
