// =====================================================================
//  MOTION SWORD - Bluetooth version with LOOK AROUND (no Python needed)
// =====================================================================
//  The ESP32 pretends to be a Bluetooth mouse + keyboard. Pair it with
//  the Mac like any Bluetooth mouse. Then:
//    - Turn / tilt the sword          -> mouse moves (camera looks around)
//    - Swing the sword hard           -> left click (attack)
//    - Hold it sideways and still     -> hold right click (shield blocks)
//    - Push the joystick              -> hold W A S D (walk)
//    - Press the joystick down        -> hold Space (jump)
//
//  SETUP (once):
//    1. Boards Manager: set "esp32 by Espressif Systems" to version 2.0.17
//       (the Bluetooth library doesn't compile on 3.x yet).
//    2. Download the ZIP of https://github.com/blackketter/ESP32-BLE-Combo
//       (green Code button -> Download ZIP), then
//       Sketch -> Include Library -> Add .ZIP Library... and pick it.
//    3. Also needs the "Adafruit MPU6050" library.
//
//  STARTING UP: lay the sword down STILL and don't touch the joystick for
//  2 seconds after power-on. It measures the sensor's resting values so
//  the camera doesn't slowly drift.
//
//  Wiring:
//    MPU6050:  VCC -> + rail, GND -> - rail, SDA -> D21, SCL -> D22
//    Joystick: +5V -> + rail (3.3V), GND -> - rail, VRx -> D34, VRy -> D35, SW -> D32
//    ESP32:    3V3 -> + rail, GND -> - rail
//
//  Blue LED:  slow blink = waiting to pair, solid = connected,
//             fast blink = motion sensor not found (check its wires)
// =====================================================================

#include <BleCombo.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// ---------- PINS ----------
const int JOY_X_PIN = 34;
const int JOY_Y_PIN = 35;
const int JOY_BTN_PIN = 32;
const int LED_PIN = 2;

// ---------- LOOKING AROUND (gyro aiming) ----------
const bool USE_GYRO_LOOK = true;   // false = turn camera control off
const bool LOOK_UP_DOWN = false;   // false = only left/right (easier); true = also up/down

// Which gyro axis is "turning left/right" and which is "tilting up/down".
// It depends on how the sensor is mounted. To find out: set PRINT_GYRO to
// true, upload, open Tools -> Serial Plotter, then turn the sword
// left/right (one line jumps = that's LOOK_H_AXIS) and tilt it up/down
// (another line jumps = that's LOOK_V_AXIS).
const char LOOK_H_AXIS = 'z';
const char LOOK_V_AXIS = 'y';
const bool FLIP_H = false;         // true if turning right looks left
const bool FLIP_V = false;         // true if tilting up looks down
const bool PRINT_GYRO = false;     // true = print gyro values for the Plotter

const float LOOK_SENSITIVITY = 8.0;  // bigger = camera turns faster
const float LOOK_DEADZONE = 0.08;    // ignore tiny hand shakes (rad/s)
const float LOOK_MAX_SPEED = 4.0;    // turning faster than this = a swing, not aiming
const float LOOK_FREEZE_ACCEL = 14.0;      // freeze the camera while the sword is jerking
const unsigned long LOOK_FREEZE_MS = 400;  // ...and for this long after a swing

// ---------- ATTACK / BLOCK ----------
const float SWING_THRESHOLD = 22.0;       // bigger = need a harder swing (still = 9.8)
const unsigned long SWING_COOLDOWN = 350; // ms between swings, so 1 swing = 1 hit

// TWO ATTACKS: side slash = left click (basic), overhead chop = right click (special).
// Each swing prints which gyro axis it spun around, e.g. "swing axis: z".
// Do a few overhead chops, see which letter shows up, and put it here.
const char SPECIAL_AXIS = 'x';
const unsigned long SWING_WINDOW_MS = 120; // how long to watch a swing before deciding its type

const bool USE_GUARD = false;             // block pose holds right click; off because right click = special attack
const char GUARD_AXIS = 'y';              // which accel axis reads ~9.8 in your guard pose
const float GUARD_MIN = 7.5;
const unsigned long GUARD_HOLD_MS = 250;  // hold the pose this long before blocking

// ---------- WALKING ----------
const bool USE_JOYSTICK = true;           // set false if the joystick isn't wired yet
const int JOY_DEADZONE = 700;             // how far to push before you walk (0-2048)
const bool FLIP_FB = false;               // true if forward/back are reversed
const bool FLIP_LR = false;               // true if left/right are reversed
// -------------------------------------------------

Adafruit_MPU6050 mpu;

unsigned long lastSwing = 0;
bool inSwing = false;
unsigned long swingStart = 0;
float peakX = 0, peakY = 0, peakZ = 0;
unsigned long guardStart = 0;
bool guarding = false;

int joyCenterX = 2048, joyCenterY = 2048;
float gyroBiasX = 0, gyroBiasY = 0, gyroBiasZ = 0;
float lookAccH = 0, lookAccV = 0;   // leftover fractions of a mouse step

bool heldW = false, heldA = false, heldS = false, heldD = false;
bool heldSpace = false, heldRight = false;
bool wasConnected = false;

void setKey(bool& held, bool want, uint8_t key) {
  if (want && !held) { Keyboard.press(key); held = true; }
  if (!want && held) { Keyboard.release(key); held = false; }
}

void setRightClick(bool want) {
  if (want && !heldRight) { Mouse.press(MOUSE_RIGHT); heldRight = true; Serial.println("block ON"); }
  if (!want && heldRight) { Mouse.release(MOUSE_RIGHT); heldRight = false; Serial.println("block off"); }
}

float pickAxis(char axis, float x, float y, float z) {
  return (axis == 'x') ? x : (axis == 'y') ? y : z;
}

int clampMove(int v) {
  if (v > 127) return 127;
  if (v < -127) return -127;
  return v;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(LED_PIN, OUTPUT);
  pinMode(JOY_BTN_PIN, INPUT_PULLUP);

  if (!mpu.begin()) {
    Serial.println("MPU6050 not found. Check the SDA/SCL/VCC/GND wires!");
    while (true) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(100);
    }
  }
  Serial.println("MPU6050 found!");
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_1000_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  // Calibrate: measure the resting values while nothing is moving
  Serial.println("Calibrating... keep the sword still and don't touch the joystick.");
  digitalWrite(LED_PIN, HIGH);
  long sx = 0, sy = 0;
  float gx = 0, gy = 0, gz = 0;
  const int N = 200;
  for (int i = 0; i < N; i++) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    gx += g.gyro.x; gy += g.gyro.y; gz += g.gyro.z;
    if (USE_JOYSTICK) {
      sx += analogRead(JOY_X_PIN);
      sy += analogRead(JOY_Y_PIN);
    }
    delay(5);
  }
  gyroBiasX = gx / N; gyroBiasY = gy / N; gyroBiasZ = gz / N;
  if (USE_JOYSTICK) { joyCenterX = sx / N; joyCenterY = sy / N; }
  Serial.println("Calibration done.");

  Keyboard.begin();
  Mouse.begin();
  Serial.println("Bluetooth on. Pair it from your Mac's Bluetooth settings.");
}

void loop() {
  unsigned long now = millis();
  bool connected = Keyboard.isConnected();

  digitalWrite(LED_PIN, connected ? HIGH : ((now / 500) % 2));
  if (connected && !wasConnected) Serial.println("Connected to computer!");
  if (!connected && wasConnected) {
    Serial.println("Disconnected.");
    heldW = heldA = heldS = heldD = heldSpace = heldRight = false;
  }
  wasConnected = connected;

  // ---- Read the motion sensor ----
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  float ax = a.acceleration.x, ay = a.acceleration.y, az = a.acceleration.z;
  float total = sqrt(ax * ax + ay * ay + az * az);
  float gx = g.gyro.x - gyroBiasX;
  float gy = g.gyro.y - gyroBiasY;
  float gz = g.gyro.z - gyroBiasZ;

  if (PRINT_GYRO) {
    Serial.print("gx:"); Serial.print(gx);
    Serial.print(" gy:"); Serial.print(gy);
    Serial.print(" gz:"); Serial.println(gz);
  }

  // ---- SWING -> left click (slash) or right click (chop) ----
  // A spike in acceleration starts a swing; then we watch the gyro briefly
  // to see which way the sword spun.
  if (!inSwing && total > SWING_THRESHOLD && now - lastSwing > SWING_COOLDOWN) {
    inSwing = true;
    swingStart = now;
    peakX = peakY = peakZ = 0;
    guarding = false;
    guardStart = 0;
  }
  if (inSwing) {
    peakX = max(peakX, (float)fabs(gx));
    peakY = max(peakY, (float)fabs(gy));
    peakZ = max(peakZ, (float)fabs(gz));
    if (now - swingStart > SWING_WINDOW_MS) {
      char axis = (peakX >= peakY && peakX >= peakZ) ? 'x' : (peakY >= peakZ) ? 'y' : 'z';
      bool special = (axis == SPECIAL_AXIS);
      inSwing = false;
      lastSwing = now;
      if (connected) {
        setRightClick(false);
        Mouse.click(special ? MOUSE_RIGHT : MOUSE_LEFT);
      }
      if (!PRINT_GYRO) {
        Serial.print(special ? "SPECIAL ATTACK (right click)" : "ATTACK (left click)");
        Serial.print("   swing axis: ");
        Serial.println(axis);
      }
    }
  }

  // ---- GUARD -> hold right click ----
  float guardValue = pickAxis(GUARD_AXIS, ax, ay, az);
  bool still = total > 8.0 && total < 11.5;
  bool inPose = USE_GUARD && fabs(guardValue) > GUARD_MIN && still;
  bool recentlySwung = inSwing || now - lastSwing < SWING_COOLDOWN;
  if (inPose && !recentlySwung) {
    if (guardStart == 0) guardStart = now;
    if (now - guardStart > GUARD_HOLD_MS) guarding = true;
  } else {
    guardStart = 0;
    if (!inPose) guarding = false;
  }

  // ---- LOOK AROUND -> move the mouse ----
  int moveX = 0, moveY = 0;
  if (USE_GYRO_LOOK) {
    float h = pickAxis(LOOK_H_AXIS, gx, gy, gz);
    float v = pickAxis(LOOK_V_AXIS, gx, gy, gz);
    if (FLIP_H) h = -h;
    if (FLIP_V) v = -v;

    bool swinging = inSwing || total > LOOK_FREEZE_ACCEL ||
                    now - lastSwing < LOOK_FREEZE_MS ||
                    fabs(h) > LOOK_MAX_SPEED || fabs(v) > LOOK_MAX_SPEED;

    if (swinging) {
      lookAccH = 0; lookAccV = 0;      // don't let a swing jerk the camera
    } else {
      if (fabs(h) < LOOK_DEADZONE) h = 0;
      if (fabs(v) < LOOK_DEADZONE) v = 0;
      // Turning right should look right; the minus signs match the
      // usual sensor direction. Use FLIP_H / FLIP_V if yours is opposite.
      lookAccH += -h * LOOK_SENSITIVITY;
      lookAccV += -v * LOOK_SENSITIVITY;
      moveX = (int)lookAccH; lookAccH -= moveX;
      moveY = (int)lookAccV; lookAccV -= moveY;
      if (!LOOK_UP_DOWN) { moveY = 0; lookAccV = 0; }
    }
  }

  // ---- WALK with the joystick ----
  int fb = 0, lr = 0;
  bool jump = false;
  if (USE_JOYSTICK) {
    int dx = analogRead(JOY_X_PIN) - joyCenterX;
    int dy = analogRead(JOY_Y_PIN) - joyCenterY;
    lr = (dx > JOY_DEADZONE) ? 1 : (dx < -JOY_DEADZONE) ? -1 : 0;
    fb = (dy < -JOY_DEADZONE) ? 1 : (dy > JOY_DEADZONE) ? -1 : 0;
    if (FLIP_LR) lr = -lr;
    if (FLIP_FB) fb = -fb;
    jump = digitalRead(JOY_BTN_PIN) == LOW;
  }

  // ---- Send it all to the computer ----
  if (connected) {
    if (moveX != 0 || moveY != 0) Mouse.move(clampMove(moveX), clampMove(moveY));
    setRightClick(guarding);
    setKey(heldW, fb == 1, 'w');
    setKey(heldS, fb == -1, 's');
    setKey(heldD, lr == 1, 'd');
    setKey(heldA, lr == -1, 'a');
    setKey(heldSpace, jump, ' ');
  }

  delay(10);
}
