"""Sweep PWM frequencies on GP17 to find good ones for the attached speaker.

Steps through frequencies from F_LOW to F_HIGH, holding each for HOLD_MS.
Prints the current frequency so you can note which ones sound best.
"""
import time
from machine import Pin, PWM

PIN     = 17
F_LOW   = 100      # Hz
F_HIGH  = 5000     # Hz
STEP    = 100      # Hz between steps
HOLD_MS = 500      # how long to hold each frequency

pwm = PWM(Pin(PIN))
pwm.duty_u16(32768)  # 50%

print("Sweeping %d-%d Hz, step %d, hold %dms" % (F_LOW, F_HIGH, STEP, HOLD_MS))
try:
    while True:
        for f in range(F_LOW, F_HIGH + 1, STEP):
            pwm.freq(f)
            print("%d Hz" % f)
            time.sleep_ms(HOLD_MS)
finally:
    pwm.deinit()
    Pin(PIN, Pin.IN)
