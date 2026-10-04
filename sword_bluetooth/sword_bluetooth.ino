#include <Wire.h>
#include <BleCombo.h>

const int MPU_ADDR = 0x68;
const float swingThreshold = 19000.0;

const int joyX = 34;
const int joyY = 35;
const int joySW = 32;

const int lowThreshold = 900;
const int highThreshold = 3100;

const unsigned long holdTime = 400;
const unsigned long doubleTapTime = 250;

const int gyroDeadzone = 700;
const int gyroSensitivity = 800;

const float jumpThreshold = 28000.0;
const unsigned long jumpCooldown = 700;

bool alreadySwung = false;

char xKey = 0;
char yKey = 0;

bool joyButtonDown = false;
bool rightHeld = false;
bool waitingForSecondTap = false;

unsigned long joyPressTime = 0;
unsigned long firstTapTime = 0;
unsigned long lastJump = 0;

void setup() {
  Serial.begin(115200);

  pinMode(joySW, INPUT_PULLUP);

  Keyboard.begin();
  Mouse.begin();

  Wire.begin(21, 22);

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0);
  Wire.endTransmission(true);
}

void loop() {
  if (!Keyboard.isConnected()) {
    delay(20);
    return;
  }

  int x = analogRead(joyX);
  int y = analogRead(joyY);

  char newXKey = 0;
  char newYKey = 0;

  if (x < lowThreshold)
    newXKey = 'w';
  else if (x > highThreshold)
    newXKey = 's';

  if (y < lowThreshold)
    newYKey = 'd';
  else if (y > highThreshold)
    newYKey = 'a';

  if (newXKey != xKey) {
    if (xKey != 0)
      Keyboard.release(xKey);

    if (newXKey != 0)
      Keyboard.press(newXKey);

    xKey = newXKey;
  }

  if (newYKey != yKey) {
    if (yKey != 0)
      Keyboard.release(yKey);

    if (newYKey != 0)
      Keyboard.press(newYKey);

    yKey = newYKey;
  }

  bool joyState = digitalRead(joySW);

  if (joyState == LOW && !joyButtonDown) {
    joyButtonDown = true;
    joyPressTime = millis();
  }

  if (joyState == LOW && joyButtonDown && !rightHeld) {
    if (millis() - joyPressTime >= holdTime) {
      Mouse.press(MOUSE_RIGHT);
      rightHeld = true;
      waitingForSecondTap = false;
    }
  }

  if (joyState == HIGH && joyButtonDown) {
    if (rightHeld) {
      Mouse.release(MOUSE_RIGHT);
      rightHeld = false;
    } else {
      if (waitingForSecondTap &&
          millis() - firstTapTime <= doubleTapTime) {

        Keyboard.press('x');
        delay(40);
        Keyboard.release('x');

        waitingForSecondTap = false;

      } else {
        waitingForSecondTap = true;
        firstTapTime = millis();
      }
    }

    joyButtonDown = false;
  }

  if (waitingForSecondTap &&
      millis() - firstTapTime > doubleTapTime) {

    Mouse.click(MOUSE_RIGHT);
    waitingForSecondTap = false;
  }

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 6, true);

  int16_t AcX = Wire.read() << 8 | Wire.read();
  int16_t AcY = Wire.read() << 8 | Wire.read();
  int16_t AcZ = Wire.read() << 8 | Wire.read();

  if (AcX < -swingThreshold && !alreadySwung) {
    Mouse.click(MOUSE_LEFT);
    alreadySwung = true;
  }

  if (AcX > -12000) {
    alreadySwung = false;
  }

  if (abs(AcZ) > jumpThreshold &&
      abs(AcX) < swingThreshold &&
      millis() - lastJump > jumpCooldown) {

    Keyboard.press(' ');
    delay(40);
    Keyboard.release(' ');

    lastJump = millis();
  }

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x43);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 6, true);

  int16_t GyX = Wire.read() << 8 | Wire.read();
  int16_t GyY = Wire.read() << 8 | Wire.read();
  int16_t GyZ = Wire.read() << 8 | Wire.read();

  int mouseX = 0;

  if (abs(GyZ) > gyroDeadzone)
    mouseX = -GyZ / gyroSensitivity;

  mouseX = constrain(mouseX, -20, 20);

  if (mouseX != 0)
    Mouse.move(mouseX, 0);

  delay(10);
}
