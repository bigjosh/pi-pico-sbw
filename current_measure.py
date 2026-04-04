"""Interactive current measurement test.

Powers the target, waits for boot, switches to pull-up, and measures
current with SBW pins driven vs floating.
"""
import time
import machine
from target_power import TargetPower

POWER_PIN = 28

power = TargetPower(POWER_PIN)
r_pullup = TargetPower.load_calibration()
print("R_pullup: %.0f ohm" % r_pullup)

power.on()
print("Powered on, waiting 2s for boot...")
time.sleep_ms(2000)

print("Measuring with SBW pins driven...")
power.power_via_pullup()
time.sleep_ms(200)
ua_driven = power.measure_current_ua(r_pullup)
power.on()  # restore power

print("Floating SBW pins...")
machine.Pin(26, machine.Pin.IN)  # clock
machine.Pin(27, machine.Pin.IN)  # data
time.sleep_ms(100)

print("Measuring with SBW pins floating...")
power.power_via_pullup()
time.sleep_ms(200)
ua = power.measure_current_ua(r_pullup)

print("Driven: %s uA" % ("%.1f" % ua_driven if ua_driven else "brownout"))
print("Floating: %s uA" % ("%.1f" % ua if ua else "brownout"))

power.off()
print("Done.")
