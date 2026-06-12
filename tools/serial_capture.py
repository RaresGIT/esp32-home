"""Capture serial output from the board, optionally hard-resetting it first.

Usage: python tools/serial_capture.py [seconds] [--reset]
"""
import sys
import time

import serial

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

PORT = "COM3"
seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 20
do_reset = "--reset" in sys.argv

p = serial.Serial(PORT, 115200, timeout=0.5)
if do_reset:
    # USB-Serial/JTAG reset sequence (same as esptool's hard reset)
    p.dtr = False
    p.rts = True
    time.sleep(0.1)
    p.rts = False
    p.dtr = False

t0 = time.time()
while time.time() - t0 < seconds:
    data = p.read(4096)
    if data:
        sys.stdout.write(data.decode("utf-8", "replace"))
        sys.stdout.flush()
p.close()
