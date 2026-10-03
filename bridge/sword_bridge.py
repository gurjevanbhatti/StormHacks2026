"""
MOTION SWORD - laptop bridge
Turns messages from the sword into mouse clicks and key presses for Minecraft.

Setup (once):
    pip install pyserial pynput

Run it ONE of two ways:
    Over USB cable (best for testing):
        python sword_bridge.py COM3                      (Windows)
        python sword_bridge.py /dev/cu.usbserial-0001    (Mac)
    Over Wi-Fi (laptop must be on the same phone hotspot as the sword):
        python sword_bridge.py wifi

Then click into the Minecraft window and play. Ctrl+C here to stop.

Controls it creates:
    SWING            -> left click (attack)
    guard            -> hold right click (shield in your OFFHAND blocks)
    joystick         -> hold W / A / S / D
    joystick pressed -> hold space (jump)

Safety: if the sword goes quiet for 1 second (out of range, battery dead,
cable pulled), every key and button is released so you don't run off a cliff.
"""

import socket
import sys
import time

from pynput.keyboard import Controller as KeyboardController
from pynput.keyboard import Key
from pynput.mouse import Button
from pynput.mouse import Controller as MouseController

PORT = 4210              # must match sword_wireless.ino
BAUD = 115200
TIMEOUT_SECONDS = 1.0

mouse = MouseController()
keyboard = KeyboardController()

held_keys = set()
blocking = False
last_swing_id = None
last_message_time = time.time()


# ---------- turning messages into controls ----------

def hold(key):
    if key not in held_keys:
        keyboard.press(key)
        held_keys.add(key)


def let_go(key):
    if key in held_keys:
        keyboard.release(key)
        held_keys.discard(key)


def set_block(on):
    global blocking
    if on and not blocking:
        mouse.press(Button.right)
        blocking = True
        print("block ON")
    elif not on and blocking:
        mouse.release(Button.right)
        blocking = False
        print("block off")


def release_everything():
    for k in list(held_keys):
        let_go(k)
    set_block(False)


def apply_state(fb, lr, guard, jump):
    # forward / back
    if fb == 1:
        hold("w"); let_go("s")
    elif fb == -1:
        hold("s"); let_go("w")
    else:
        let_go("w"); let_go("s")
    # left / right
    if lr == 1:
        hold("d"); let_go("a")
    elif lr == -1:
        hold("a"); let_go("d")
    else:
        let_go("a"); let_go("d")
    # jump
    if jump:
        hold(Key.space)
    else:
        let_go(Key.space)
    # block
    set_block(bool(guard))


def handle(line):
    global last_swing_id, last_message_time
    line = line.strip()
    if not line:
        return
    last_message_time = time.time()
    parts = line.split()

    if parts[0] == "SWING":
        swing_id = parts[1] if len(parts) > 1 else None
        if swing_id is not None and swing_id == last_swing_id:
            return                      # the duplicate copy, ignore it
        last_swing_id = swing_id
        set_block(False)                # can't attack while blocking
        mouse.click(Button.left)
        print("ATTACK (left click)")

    elif parts[0] == "SPECIAL":
        swing_id = parts[1] if len(parts) > 1 else None
        if swing_id is not None and swing_id == last_swing_id:
            return                      # the duplicate copy, ignore it
        last_swing_id = swing_id
        set_block(False)
        mouse.click(Button.right)
        print("SPECIAL ATTACK (right click)")

    elif parts[0] == "STATE" and len(parts) == 5:
        try:
            fb, lr, guard, jump = (int(p) for p in parts[1:])
        except ValueError:
            return
        apply_state(fb, lr, guard, jump)

    else:
        print(f"(sword says: {line})")


def check_timeout():
    if time.time() - last_message_time > TIMEOUT_SECONDS:
        if held_keys or blocking:
            print("Sword went quiet - releasing all controls.")
            release_everything()


# ---------- the two ways to listen ----------

def run_wifi():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("", PORT))
    sock.settimeout(0.1)

    # Show the laptop's IP, in case you need to put it in the .ino file
    try:
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        probe.connect(("8.8.8.8", 80))
        print(f"Laptop IP: {probe.getsockname()[0]}")
        probe.close()
    except OSError:
        print("Laptop IP: (couldn't tell - are you on the hotspot?)")

    print(f"Listening for the sword on Wi-Fi port {PORT}...")
    heard_anything = False
    while True:
        try:
            data, addr = sock.recvfrom(256)
            if not heard_anything:
                print(f"Sword found at {addr[0]}! Click into Minecraft.")
                heard_anything = True
            for line in data.decode(errors="ignore").splitlines():
                handle(line)
        except socket.timeout:
            pass
        check_timeout()


def run_serial(port):
    import serial  # only needed for USB mode
    print(f"Connecting to {port}...")
    ser = serial.Serial(port, BAUD, timeout=0.1)
    print("Connected over USB! Click into Minecraft.")
    try:
        while True:
            line = ser.readline().decode(errors="ignore")
            if line:
                handle(line)
            check_timeout()
    finally:
        ser.close()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    mode = sys.argv[1]
    try:
        if mode.lower() == "wifi":
            run_wifi()
        else:
            run_serial(mode)
    except KeyboardInterrupt:
        print("\nStopping.")
    finally:
        release_everything()
