"""Calibrate the internal pull-up resistance on the VCC pin.

Connect a known resistor (default 1.1M ohm, 1% tolerance) from the VCC pin
(GP28, Pico pin 34) to GND. Then run:

    import current_measure_calibrate

The measured pull-up resistance is saved to calibration.json for use by
the programming loop's current measurement.
"""
import json
import machine
from target_power import _adc_with_pullup, _adc_avg_ms

POWER_PIN = 28
CAL_RESISTOR_OHMS = 1_100_000
CAL_FILE = "calibration.json"

print("Pull-up calibration")
print("Ensure a %.0f ohm resistor is connected from GP%d to GND." % (CAL_RESISTOR_OHMS, POWER_PIN))
print("Measuring...")

adc = _adc_with_pullup(POWER_PIN)
avg = _adc_avg_ms(adc, 500)
v_cal = avg * 3.3 / 65535
if v_cal < 0.1:
    raise RuntimeError("calibration voltage too low — check resistor connection")
r_pullup = CAL_RESISTOR_OHMS * (3.3 / v_cal - 1)
machine.Pin(POWER_PIN, machine.Pin.IN)  # restore to hi-Z

print("Measured pull-up resistance: %.0f ohm" % r_pullup)

with open(CAL_FILE, "w") as f:
    json.dump({"r_pullup_ohms": r_pullup}, f)

print("Saved to %s" % CAL_FILE)
