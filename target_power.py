import time

import machine


TARGET_DETECT_MV = 3000
TARGET_DETECT_HOLD_MS = 20

_PADS_BANK0_BASE = 0x40038000  # RP2350 pad control register block

# Current measurement defaults
CURRENT_WINDOW_MS = 100        # ADC averaging window (should cover load noise cycles)
BROWNOUT_MV = 1700             # below this the target has browned out


def _adc_with_pullup(pin_num):
    """Configure a pin as ADC input with pull-up enabled.

    The Pico SDK's adc_gpio_init() (called internally by machine.ADC())
    calls gpio_disable_pulls(), which strips the pull-up we need.
    We re-enable it by setting the PUE bit (bit 3) in the RP2350
    PADS_BANK0 register for this GPIO.
    """
    adc = machine.ADC(pin_num)
    pad_reg = _PADS_BANK0_BASE + 4 + pin_num * 4
    machine.mem32[pad_reg] |= 1 << 3  # PUE — re-enable pull-up
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

    @staticmethod
    def load_calibration(path="calibration.json"):
        """Load pull-up resistance from a calibration JSON file."""
        import json
        try:
            with open(path) as f:
                return json.load(f)["r_pullup_ohms"]
        except OSError:
            raise RuntimeError("calibration.json not found — see current_measure.md Calibration section")

    def power_via_pullup(self):
        """Switch VCC from GPIO drive to internal pull-up.

        Call this, wait for the RC circuit to settle, then call
        measure_current_ua(). Caller controls the settle time.
        """
        _adc_with_pullup(self._power_pin)

    def measure_current_ua(self, r_pullup, window_ms=CURRENT_WINDOW_MS, brownout_mv=BROWNOUT_MV):
        """Measure target current via the pull-up voltage drop.

        r_pullup is the calibrated pull-up resistance in ohms (from
        load_calibration()).

        Averages ADC readings over window_ms milliseconds. The window should
        be long enough to cover any periodic load variation (e.g. LCD toggle).

        Caller must have already called power_via_pullup() and waited for the
        RC circuit to settle before calling this.

        If the voltage is below brownout_mv, the target has browned out and
        the function restores GPIO drive and returns None.

        On success, returns the measured current in µA.
        """
        adc = machine.ADC(self._power_pin)
        avg = _adc_avg_ms(adc, window_ms)
        v_meas = avg * 3.3 / 65535

        if v_meas * 1000 < brownout_mv:
            self._power.init(machine.Pin.OUT, value=1)  # rescue
            return None

        i_amps = (3.3 - v_meas) / r_pullup
        return i_amps * 1_000_000  # µA


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


def _adc_avg_ms(adc, window_ms):
    """Sample ADC continuously for window_ms and return the average as u16."""
    total = 0
    count = 0
    deadline = time.ticks_add(time.ticks_ms(), window_ms)
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        total += adc.read_u16()
        count += 1
    return total // count if count > 0 else 0


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
