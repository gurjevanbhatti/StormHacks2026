# NICHIRIN BLADE

A motion-controlled Minecraft sword built for StormHacks. Swing a real (foam) sword and your character attacks in Minecraft. Built for Demon Slayer mode: a side slash does the basic attack and an overhead chop does the special attack. Turn the sword to look around, and use the thumb joystick on the handle to walk and jump.

No Minecraft mods needed: the sword acts as a mouse and keyboard.

| With the sword | Computer gets | In Minecraft |
| --- | --- | --- |
| Side slash | Left click | Basic attack |
| Overhead chop | Right click | Special attack |
| Turn the sword left / right | Mouse movement | Look around (Bluetooth version) |
| Push the joystick | W / A / S / D | Walk |
| Press the joystick down | Space | Jump |

![Full wiring](images/step5_full.png)

## Two ways to run it

| Version | Files | How it reaches the computer |
| --- | --- | --- |
| **Bluetooth (recommended)** | `sword_bluetooth/` | The ESP32 pairs as a Bluetooth mouse + keyboard. No laptop software. Includes look-around. |
| USB / Wi-Fi + Python | `sword_wireless/` + `bridge/` | The ESP32 sends messages over USB or Wi-Fi; a Python script on the laptop clicks and presses keys. |

## Hardware

- ESP32 dev board (30-pin, USB-C, CP2102)
- MPU6050 (GY-521) accelerometer + gyroscope
- Joystick module
- Breadboard and jumper wires (male-to-male and female-to-male)
- 9V battery + clip (into **VIN**), or a USB power bank

## Wiring

| From | To |
| --- | --- |
| ESP32 3V3 | + rail |
| ESP32 GND | − rail |
| MPU6050 VCC / GND | + rail / − rail |
| MPU6050 SDA | D21 |
| MPU6050 SCL | D22 |
| Joystick +5V / GND | + rail (3.3V) / − rail |
| Joystick VRx | D34 |
| Joystick VRy | D35 |
| Joystick SW | D32 |
| 9V battery red / black | VIN / GND (never 3V3 or the + rail) |

## Files

| Path | What it is |
| --- | --- |
| `sword_bluetooth/sword_bluetooth.ino` | Full sword as a Bluetooth mouse + keyboard: two attacks, look-around, joystick |
| `sword_wireless/sword_wireless.ino` | Full sword for the Python bridge (USB or Wi-Fi): two attacks, joystick |
| `bridge/sword_bridge.py` | Laptop script that turns the sword's messages into clicks and key presses |
| `sword_test/sword_test.ino` | Prints motion sensor readings for the Serial Plotter |
| `i2c_check/i2c_check.ino` | Diagnoses "MPU6050 not found": wiring problem vs clone chip |
| `images/` | Wiring diagrams for each build step |

## Setup: Bluetooth version

1. Arduino IDE → Boards Manager → **esp32 by Espressif Systems**, version **2.0.17** (the Bluetooth library doesn't compile on 3.x yet).
2. Install [ESP32-BLE-Combo](https://github.com/blackketter/ESP32-BLE-Combo): Download ZIP → Sketch → Include Library → Add .ZIP Library.
3. Install the **Adafruit MPU6050** library.
4. Board **ESP32 Dev Module**, upload `sword_bluetooth.ino`.
5. Lay the sword still for 2 seconds while it starts (calibration).
6. Pair **ESP32 Keyboard/Mouse** in the computer's Bluetooth settings. The blue LED goes solid when connected.

## Setup: Python version

1. Add `https://espressif.github.io/arduino-esp32/package_esp32_index.json` to *Additional boards manager URLs* and install **esp32 by Espressif Systems**.
2. Install the **Adafruit MPU6050** library, select **ESP32 Dev Module**.
3. For Wi-Fi, set `WIFI_NAME` and `WIFI_PASS` in `sword_wireless.ino` (2.4 GHz hotspot). Upload.
4. On the laptop:

```bash
cd bridge
pip install -r requirements.txt

python3 sword_bridge.py /dev/cu.usbserial-0001   # USB, Mac
python sword_bridge.py COM3                      # USB, Windows
python3 sword_bridge.py wifi                     # Wi-Fi (same hotspot)
```

On a Mac, allow Terminal under System Settings → Privacy & Security → Accessibility.

## Tuning

All settings are at the top of each `.ino` file.

- **`SPECIAL_AXIS`**: each swing prints `swing axis: x/y/z` in the Serial Monitor. Do a few overhead chops and set this to the letter they show.
- **`SWING_THRESHOLD`**: lower if swings are missed, higher if it attacks by accident.
- **`LOOK_H_AXIS`, `FLIP_H`, `LOOK_SENSITIVITY`**: camera turning (Bluetooth version). Set `PRINT_GYRO = true` to see which axis moves in the Serial Plotter.
- **`FLIP_FB`, `FLIP_LR`, `JOY_DEADZONE`**: joystick direction and sensitivity.

## How it works

The ESP32 reads the MPU6050 about 100 times a second. A spike in total acceleration above `SWING_THRESHOLD` starts a swing; for the next 120 ms it records the peak rotation on each gyro axis, and the axis the sword spun around most decides the attack type. Turning the sword (gyro rate, with a deadzone and startup bias calibration) moves the mouse, and the camera freezes during swings so attacks don't jerk the view. The joystick is read as analog X/Y with a calibrated center and mapped to WASD.
