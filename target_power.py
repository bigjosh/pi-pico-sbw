import time

import machine


TARGET_DETECT_MV = 3000
TARGET_DETECT_HOLD_MS = 20

_PADS_BANK0_BASE = 0x40038000


def _adc_with_pullup(pin_num):
    """Configure a pin as ADC input with pull-up enabled (for target detect)."""
    adc = machine.ADC(pin_num)
    pad_reg = _PADS_BANK0_BASE + 4 + pin_num * 4
    machine.mem32[pad_reg] |= 1 << 3  # PUE
    return adc


class TargetPower:
    def __init__(self, power_pin, settle_ms=0):
        self._power_pin = power_pin
        self._power = machine.Pin(power_pin, machine.Pin.IN)
        self._settle_ms = settle_ms

    def on(self):
        self._power.init(machine.Pin.OUT, value=1)
        if self._settle_ms > 0:
            time.sleep_ms(self._settle_ms)

    def off(self):
        self._power.init(machine.Pin.OUT, value=0)

    def measure_current_ua(self, res_pin, r_ext, vcc=3.32, settle_ms=500, sample_ms=500):
        """Measure target current via external burden resistor.

        Drives res_pin high through the external resistor while reading
        the power pin voltage via ADC. Returns (min, avg, max) current in µA.

        res_pin: GPIO number connected to the resistor's drive side
        r_ext: external resistor value in ohms
        vcc: measured supply voltage
        settle_ms: time to wait after switching to resistor power
        sample_ms: sampling window in ms (1 sample per ms)
        """
        # Drive resistor pin high, switch power pin to ADC
        machine.Pin(res_pin, machine.Pin.OUT, value=1)
        adc = machine.ADC(self._power_pin)
        time.sleep_ms(settle_ms)

        total = 0
        min_raw = 65535
        max_raw = 0
        for _ in range(sample_ms):
            raw = adc.read_u16()
            total += raw
            if raw < min_raw:
                min_raw = raw
            if raw > max_raw:
                max_raw = raw
            time.sleep_ms(1)

        # Restore: power pin drives high, float resistor pin
        machine.Pin(self._power_pin, machine.Pin.OUT, value=1)
        machine.Pin(res_pin, machine.Pin.IN)

        def raw_to_ua(r):
            return (vcc - r * vcc / 65535) / r_ext * 1_000_000

        avg = total // sample_ms
        return (raw_to_ua(min_raw), raw_to_ua(avg), raw_to_ua(max_raw))


class TargetPowerWithDetect(TargetPower):
    def __init__(self, power_pin, detect_pin, settle_ms=0):
        super().__init__(power_pin, settle_ms)
        self._detect_pin = detect_pin

    def wait_for_connect(self, threshold_mv=TARGET_DETECT_MV, hold_ms=TARGET_DETECT_HOLD_MS):
        """Block until a target is detected on the detect pin."""
        adc = _adc_with_pullup(self._detect_pin)
        threshold_u16 = int(threshold_mv * 65535 // 3300)
        _wait_adc(adc, threshold_u16, hold_ms, below=True)
        self._restore_detect_pin()

    def wait_for_disconnect(self, threshold_mv=TARGET_DETECT_MV, hold_ms=TARGET_DETECT_HOLD_MS):
        """Block until the target is removed from the detect pin."""
        adc = _adc_with_pullup(self._detect_pin)
        threshold_u16 = int(threshold_mv * 65535 // 3300)
        _wait_adc(adc, threshold_u16, hold_ms, below=False)
        self._restore_detect_pin()

    def _restore_detect_pin(self):
        """Restore the detect pin to GPIO output low after ADC use."""
        machine.Pin(self._detect_pin, machine.Pin.OUT, value=0)

    def read_detect_mv(self):
        """Read the current voltage on the detect pin in millivolts."""
        adc = _adc_with_pullup(self._detect_pin)
        return adc.read_u16() * 3300 // 65535



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
