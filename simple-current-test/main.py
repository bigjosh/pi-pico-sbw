"""Minimal current measurement test bench.

Uses the same measure_current_ua() from target_power.py as the production
code in program_tsl.py.

Pin assignments match program_tsl.py:
  GP27 = VCC (power pin)
  GP21 = Burden resistor drive pin (100k to GP27)
  GP26 = SBWTCK (clock)
  GP22 = SBWTDIO (data)
"""
import time
import machine
from target_power import TargetPower

SBW_PIN_POWER   = 27
SBW_PIN_RES     = 21
SBW_PIN_CLOCK   = 26
SBW_PIN_DATA    = 22

BOOT_TIME_MS    = 750
SETTLE_TIME_MS  = 500
SAMPLE_TIME_MS  = 500

R_EXT = 100_000
VCC   = 3.32

# --- Setup ---
for i in range(5, 0, -1):
    print("Starting in %d..." % i)
    time.sleep(1)

print("Floating SBW pins...")
machine.Pin(SBW_PIN_CLOCK, machine.Pin.IN)
machine.Pin(SBW_PIN_DATA, machine.Pin.IN)

print("Floating resistor drive pin...")
machine.Pin(SBW_PIN_RES, machine.Pin.IN)

print("Powering down target for 1s...")
machine.Pin(SBW_PIN_POWER, machine.Pin.OUT, value=0)
time.sleep_ms(1000)

print("Powering on target for %dms..." % BOOT_TIME_MS)
power = TargetPower(SBW_PIN_POWER)
power.on()
time.sleep_ms(BOOT_TIME_MS)

print("Measuring current...")
i_min, i_avg, i_max = power.measure_current_ua(
    SBW_PIN_RES, R_EXT, vcc=VCC,
    settle_ms=SETTLE_TIME_MS, sample_ms=SAMPLE_TIME_MS
)

print("\nResults (%d ms window):" % SAMPLE_TIME_MS)
print("  Min: %.2f uA" % i_min)
print("  Avg: %.2f uA" % i_avg)
print("  Max: %.2f uA" % i_max)
