import time

import machine


TARGET_DETECT_MV = 3000
TARGET_DETECT_HOLD_MS = 20


class TargetPower:
    def __init__(self, power_pin, settle_ms=0):
        self._power = machine.Pin(power_pin, machine.Pin.IN)
        self._settle_ms = settle_ms

    def on(self):
        self._power.init(machine.Pin.OUT, value=1)
        if self._settle_ms > 0:
            time.sleep_ms(self._settle_ms)

    def off(self):
        self._power.init(machine.Pin.IN)


class TargetPowerWithDetect(TargetPower):
    def __init__(self, power_pin, detect_pin, settle_ms=0):
        super().__init__(power_pin, settle_ms)
        self._detect_pin = detect_pin

    def _adc_with_pullup(self):
        """Configure the detect pin as ADC input with pull-up enabled.

        The Pico SDK's adc_gpio_init() (called internally by machine.ADC())
        calls gpio_disable_pulls(), which strips the pull-up we need for
        target detection. We re-enable it by setting the PUE bit (bit 3)
        in the RP2350 PADS_BANK0 register for this GPIO.
        """
        adc = machine.ADC(self._detect_pin)
        _PADS_BANK0_BASE = 0x40038000  # RP2350 pad control register block
        pad_reg = _PADS_BANK0_BASE + 4 + self._detect_pin * 4
        machine.mem32[pad_reg] |= 1 << 3  # PUE — re-enable pull-up after ADC stole it
        return adc

    def wait_for_connect(self, threshold_mv=TARGET_DETECT_MV, hold_ms=TARGET_DETECT_HOLD_MS):
        """Block until a target is detected on the detect pin.

        Detection works by enabling the internal pull-up on the detect pin and
        reading it via ADC. When no target is attached the pull-up holds the
        voltage near 3.3 V. A connected MSP430 pulls SBWTCK low through its
        internal pull-down, dropping the voltage below the threshold. Returns
        once the reading stays below threshold_mv for hold_ms continuous
        milliseconds.
        """
        adc = self._adc_with_pullup()
        threshold_u16 = int(threshold_mv * 65535 // 3300)
        self._wait_adc(adc, threshold_u16, hold_ms, below=True)
        self._restore_detect_pin()

    def wait_for_disconnect(self, threshold_mv=TARGET_DETECT_MV, hold_ms=TARGET_DETECT_HOLD_MS):
        """Block until the target is removed from the detect pin.

        Returns once the ADC reading stays above threshold_mv for hold_ms
        continuous milliseconds (pull-up wins, no pull-down present).
        """
        adc = self._adc_with_pullup()
        threshold_u16 = int(threshold_mv * 65535 // 3300)
        self._wait_adc(adc, threshold_u16, hold_ms, below=False)
        self._restore_detect_pin()

    def _restore_detect_pin(self):
        """Restore the detect pin to GPIO output low after ADC use."""
        machine.Pin(self._detect_pin, machine.Pin.OUT, value=0)

    def read_detect_mv(self):
        """Read the current voltage on the detect pin in millivolts."""
        adc = self._adc_with_pullup()
        return adc.read_u16() * 3300 // 65535

    @staticmethod
    def _wait_adc(adc, threshold_u16, hold_ms, below):
        held_since = None
        while True:
            triggered = adc.read_u16() < threshold_u16 if below else adc.read_u16() >= threshold_u16
            if triggered:
                now = time.ticks_ms()
                if held_since is None:
                    held_since = now
                elif time.ticks_diff(now, held_since) >= hold_ms:
                    return
            else:
                held_since = None
            time.sleep_ms(1)
