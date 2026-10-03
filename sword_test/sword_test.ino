// Step 1 test: read the MPU6050 on an ESP32 and print motion data.
// Wiring: VCC->3.3V, GND->GND, SDA->GPIO21, SCL->GPIO22
// Libraries: "Adafruit MPU6050" (Library Manager installs its dependencies too)

#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

Adafruit_MPU6050 mpu;

void setup() {
  Serial.begin(115200);
  delay(500);

  if (!mpu.begin()) {
    Serial.println("MPU6050 not found. Check wiring!");
    while (true) delay(100);
  }
  Serial.println("MPU6050 found!");

  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);   // swings are strong, 8G gives headroom
  mpu.setGyroRange(MPU6050_RANGE_1000_DEG);       // fast wrist rotation
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
}

void loop() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // Total acceleration: about 9.8 when still, spikes when you swing
  float total = sqrt(a.acceleration.x * a.acceleration.x +
                     a.acceleration.y * a.acceleration.y +
                     a.acceleration.z * a.acceleration.z);

  // Print in a format the Arduino Serial Plotter can graph
  Serial.print("ax:"); Serial.print(a.acceleration.x);
  Serial.print(" ay:"); Serial.print(a.acceleration.y);
  Serial.print(" az:"); Serial.print(a.acceleration.z);
  Serial.print(" total:"); Serial.println(total);

  delay(20);  // ~50 readings per second
}
