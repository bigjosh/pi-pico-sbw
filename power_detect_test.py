from target_power import TargetPowerWithDetect

# Pico Pin | GPIO | Function
# ---------|------|----------
#       29 | GP22 | SBWTDIO
#       31 | GP26 | SBWTCK
#       32 | GP27 | VCC
DETECT_PIN = 26
POWER_PIN = 27

power = TargetPowerWithDetect(POWER_PIN, DETECT_PIN)

cycle = 0
while True:
    print("Waiting for target to connect...")
    power.wait_for_connect()
    mv = power.read_detect_mv()
    cycle += 1
    print("[%d] Target connected. Detect pin: %d mV" % (cycle, mv))

    print("Waiting for target to disconnect...")
    power.wait_for_disconnect()
    mv = power.read_detect_mv()
    print("[%d] Target disconnected. Detect pin: %d mV\n" % (cycle, mv))
