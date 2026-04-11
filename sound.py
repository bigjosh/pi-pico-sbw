"""Sound effects for the programming jig speaker."""
import time
from machine import Pin, PWM

_pwm = None


def init(pin):
    """Initialize PWM on the given speaker pin. Call once at startup."""
    global _pwm
    _pwm = PWM(Pin(pin))
    _pwm.duty_u16(0)


def success():
    """Double beep — 2300Hz, 50ms on / 100ms off / 50ms on."""
    _pwm.freq(2300)
    for _ in range(2):
        _pwm.duty_u16(32768)
        time.sleep_ms(50)
        _pwm.duty_u16(0)
        time.sleep_ms(100)


def fail_start():
    """Start a continuous 100Hz fail tone. Call fail_end() to stop."""
    _pwm.freq(100)
    _pwm.duty_u16(32768)


def off():
    """Stop the fail tone."""
    _pwm.duty_u16(0)
