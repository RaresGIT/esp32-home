"""Send characters/lines to the board's serial CLI and print the response.

Usage: python tools/serial_cmd.py <text> [read-seconds]
Text is sent as-is (no newline appended unless you pass one in quotes).
"""
import sys
import time

import serial

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

PORT = "COM3"
text = sys.argv[1]
seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 5

p = serial.Serial(PORT, 115200, timeout=0.5)
time.sleep(0.3)
p.reset_input_buffer()
p.write(text.encode())
p.flush()

t0 = time.time()
while time.time() - t0 < seconds:
    data = p.read(4096)
    if data:
        sys.stdout.write(data.decode("utf-8", "replace"))
        sys.stdout.flush()
p.close()
