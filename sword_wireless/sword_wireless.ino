// =====================================================================
//  MOTION SWORD - wireless version (ESP32 + MPU6050 + joystick)
// =====================================================================
//  What it does:
//    - Swing the sword hard          -> attack (left click)
//    - Hold the sword sideways, still -> block  (hold right click)
//    - Push the joystick             -> walk    (W A S D)
//    - Press the joystick down       -> jump    (space)
//
//  It sends short text messages to the laptop over BOTH:
//    - the USB cable (handy for testing), and
//    - Wi-Fi (so you can unplug the cable and swing freely)
//  The Python script sword_bridge.py turns those messages into
//  mouse clicks and key presses that Minecraft understands.
//
//  Messages:
//    SWING <number>                  one attack (sent twice, laptop ignores repeats)
//    STATE <fb> <lr> <guard> <jump>  current controls, sent 10x per second
//         fb:  1 forward, -1 back, 0 none
//         lr:  1 right,   -1 left, 0 none
//         guard, jump: 1 = held, 0 = not held
//
//  Wiring (see the build guide):
//    MPU6050:  VCC -> + rail, GND -> - rail, SDA -> D21, SCL -> D22
//    Joystick: +5V -> + rail (the 3.3V rail, on purpose), GND -> - rail,
//              VRx -> D34, VRy -> D35, SW -> D32
//    ESP32:    3V3 -> + rail, GND -> - rail
// =====================================================================

#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// ---------- 1. YOUR WI-FI (the phone hotspot) ----------
const char* WIFI_NAME = "PUT_HOTSPOT_NAME_HERE";
const char* WIFI_PASS = "PUT_HOTSPOT_PASSWORD_HERE";

// Leave this empty to "shout" to every device on the hotspot (easiest).
// If the laptop never hears anything, put the laptop's IP address here,
// e.g. "172.20.10.3" (sword_bridge.py prints it when it starts).
const char* LAPTOP_IP = "";
const int PORT = 4210;   // must match sword_bridge.py

// ---------- 2. PINS ----------
const int JOY_X_PIN = 34;
const int JOY_Y_PIN = 35;
const int JOY_BTN_PIN = 32;
const int LED_PIN = 2;    // the little blue LED on the board

// ---------- 3. TUNING (change these while testing) ----------
const float SWING_THRESHOLD = 22.0;       // bigger = need a harder swing (still = 9.8)
const unsigned long SWING_COOLDOWN = 350; // ms between swings, so 1 swing = 1 hit

// TWO ATTACKS: side slash = left click (basic), overhead chop = right click (special).
// Each swing prints which gyro axis it spun around, e.g. "swing axis: z".
// Do a few overhead chops, see which letter shows up, and put it here.
const char SPECIAL_AXIS = 'x';
const unsigned long SWING_WINDOW_MS = 120; // how long to watch a swing before deciding its type

const bool USE_GUARD = false;             // block pose holds right click; off because right click = special attack
const char GUARD_AXIS = 'y';              // which axis reads about 9.8 in your guard pose
const float GUARD_MIN = 7.5;
const unsigned long GUARD_HOLD_MS = 250;  // hold the pose this long before blocking

const int JOY_DEADZONE = 700;             // how far to push before you walk (0-2048)
const bool FLIP_FB = false;               // true if forward/back are reversed
const bool FLIP_LR = false;               // true if left/right are reversed
// ------------------------------------------------------------

Adafruit_MPU6050 mpu;
WiFiUDP udp;
IPAddress laptopIP;
bool useFixedIP = false;

unsigned long lastSwing = 0;
unsigned long swingCount = 0;
bool inSwing = false;
unsigned long swingStart = 0;
float peakX = 0, peakY = 0, peakZ = 0;
unsigned long guardStart = 0;
bool guarding = false;

int joyCenterX = 2048, joyCenterY = 2048;
int fb = 0, lr = 0;
bool jumping = false;

unsigned long lastStateSent = 0;
String lastState = "";

// Send a message over USB and Wi-Fi
void sendMsg(const String& msg) {
  Serial.println(msg);
  if (WiFi.status() == WL_CONNECTED) {
    IPAddress target = useFixedIP ? laptopIP : WiFi.broadcastIP();
    udp.beginPacket(target, PORT);
    udp.print(msg);
    udp.endPacket();
  }
}

void connectWiFi() {
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(WIFI_NAME);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_NAME, WIFI_PASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));  // blink while connecting
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_PIN, HIGH);                   // solid = connected
    Serial.print("Wi-Fi connected! Sword IP: ");
    Serial.println(WiFi.localIP());
  } else {
    digitalWrite(LED_PIN, LOW);
    Serial.println("Wi-Fi not connected (USB still works). Check name/password.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(LED_PIN, OUTPUT);
  pinMode(JOY_BTN_PIN, INPUT_PULLUP);

  if (strlen(LAPTOP_IP) > 0 && laptopIP.fromString(LAPTOP_IP)) {
    useFixedIP = true;
  }

  if (!mpu.begin()) {
    Serial.println("MPU6050 not found. Check the SDA/SCL/VCC/GND wires!");
    while (true) {                 // fast blink = sensor problem
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(100);
    }
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_1000_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  // Find the joystick's resting "center". Don't touch it while starting!
  long sx = 0, sy = 0;
  for (int i = 0; i < 50; i++) {
    sx += analogRead(JOY_X_PIN);
    sy += analogRead(JOY_Y_PIN);
    delay(5);
  }
  joyCenterX = sx / 50;
  joyCenterY = sy / 50;

  connectWiFi();
  Serial.println("READY");
}

void readMotion(unsigned long now) {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  float ax = a.acceleration.x, ay = a.acceleration.y, az = a.acceleration.z;
  float total = sqrt(ax * ax + ay * ay + az * az);

  // SWING: a big spike in total acceleration starts a swing. Then we watch
  // the gyro for a moment to see which way the sword spun.
  if (!inSwing && total > SWING_THRESHOLD && now - lastSwing > SWING_COOLDOWN) {
    inSwing = true;
    swingStart = now;
    peakX = peakY = peakZ = 0;
    guarding = false;              // swinging always drops the block
    guardStart = 0;
  }
  if (inSwing) {
    peakX = max(peakX, (float)fabs(g.gyro.x));
    peakY = max(peakY, (float)fabs(g.gyro.y));
    peakZ = max(peakZ, (float)fabs(g.gyro.z));
    if (now - swingStart > SWING_WINDOW_MS) {
      char axis = (peakX >= peakY && peakX >= peakZ) ? 'x' : (peakY >= peakZ) ? 'y' : 'z';
      Serial.print("swing axis: ");
      Serial.println(axis);
      inSwing = false;
      lastSwing = now;
      swingCount++;
      String msg = (axis == SPECIAL_AXIS ? "SPECIAL " : "SWING ") + String(swingCount);
      sendMsg(msg);
      sendMsg(msg);                // twice, in case Wi-Fi drops one
    }
  }

  // GUARD: one axis feels gravity (held sideways) and the sword is still
  float axisValue = (GUARD_AXIS == 'x') ? ax : (GUARD_AXIS == 'y') ? ay : az;
  bool still = total > 8.0 && total < 11.5;
  bool inPose = USE_GUARD && fabs(axisValue) > GUARD_MIN && still;
  bool recentlySwung = inSwing || now - lastSwing < SWING_COOLDOWN;

  if (inPose && !recentlySwung) {
    if (guardStart == 0) guardStart = now;
    if (now - guardStart > GUARD_HOLD_MS) guarding = true;
  } else {
    guardStart = 0;
    if (!inPose) guarding = false;
  }
}

void readJoystick() {
  int dx = analogRead(JOY_X_PIN) - joyCenterX;
  int dy = analogRead(JOY_Y_PIN) - joyCenterY;
  lr = (dx > JOY_DEADZONE) ? 1 : (dx < -JOY_DEADZONE) ? -1 : 0;
  fb = (dy < -JOY_DEADZONE) ? 1 : (dy > JOY_DEADZONE) ? -1 : 0;
  if (FLIP_LR) lr = -lr;
  if (FLIP_FB) fb = -fb;
  jumping = digitalRead(JOY_BTN_PIN) == LOW;   // pressed = LOW
}

void loop() {
  unsigned long now = millis();
  readMotion(now);
  readJoystick();

  // Send the current state when it changes, and 10x per second anyway.
  // Sending it over and over means a lost Wi-Fi message fixes itself.
  String state = "STATE " + String(fb) + " " + String(lr) + " " +
                 String(guarding ? 1 : 0) + " " + String(jumping ? 1 : 0);
  if (state != lastState || now - lastStateSent > 100) {
    sendMsg(state);
    lastState = state;
    lastStateSent = now;
  }

  // LED: solid when Wi-Fi is connected, off when not
  digitalWrite(LED_PIN, WiFi.status() == WL_CONNECTED ? HIGH : LOW);

  delay(10);
}
