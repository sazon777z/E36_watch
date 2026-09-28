import serial
import time
import sys

port = 'COM6'
baud = 115200

print(f"Connecting to {port} at {baud}...")
try:
    s = serial.Serial(port, baud, timeout=0.2, dsrdtr=False, rtscts=False)
    s.dtr = False
    s.rts = False
except Exception as e:
    print(f"Error opening {port}: {e}")
    sys.exit(1)

print("READY! Listening for button presses and crash logs for 45 seconds...")
print(">>> PRESS THE BUTTON NOW <<<")

log_file = open("button_test.log", "w", encoding="utf-8")
start_time = time.time()

try:
    while time.time() - start_time < 45:
        if s.in_waiting:
            raw = s.readline()
            line = raw.decode('utf-8', errors='replace').rstrip()
            if line:
                print(f"[SERIAL] {line}", flush=True)
                log_file.write(line + "\n")
                log_file.flush()
        else:
            time.sleep(0.01)
finally:
    log_file.close()
    s.close()
    print("Monitor finished.")
