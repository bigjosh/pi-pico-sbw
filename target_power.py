import time

import machine


class TargetPower:
    def __init__(self, pin, settle_ms=0):
        self._pin = machine.Pin(pin, machine.Pin.IN)
        self._settle_ms = settle_ms

    def on(self):
        self._pin.init(machine.Pin.OUT, value=1)
        if self._settle_ms > 0:
            time.sleep_ms(self._settle_ms)

    def off(self):
        self._pin.init(machine.Pin.IN)
