#!/usr/bin/env python3
"""Sets the bar clock's time from this Mac over USB (for when it has no Wi-Fi)."""
import glob, time, serial
port = (glob.glob("/dev/cu.usbmodem*") or [None])[0]
if not port: raise SystemExit("C3 not plugged in")
s = serial.Serial(port, 115200, timeout=1); time.sleep(1)
s.write(f"T{int(time.time())}\n".encode())
end = time.time() + 3
while time.time() < end:
    line = s.readline().decode(errors="ignore").strip()
    if "time set" in line: raise SystemExit("clock set")
print("no reply (is bar_clock on it?)")
