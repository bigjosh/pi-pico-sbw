import time

import machine

import sbw_native
from sbw_config import (
    BYPASS_EXPECTED,
    BYPASS_PATTERN,
    DEFAULT_HW,
    FULL_EMULATION_MASK,
    GPIO_OE_ADDR,
    GPIO_OE_CLR_ADDR,
    GPIO_OE_SET_ADDR,
    GPIO_OUT_ADDR,
    GPIO_OUT_SET_ADDR,
    POWER_MASK,
    SBW_TARGET_POWER_SETTLE_MS,
    ensure_system_clock,
)


def _mask_to_pin(mask):
    if mask == 0 or (mask & (mask - 1)) != 0:
        raise ValueError("expected one-hot gpio mask")
    pin = 0
    while mask > 1:
        mask >>= 1
        pin += 1
    return pin


class SBWNative:
    def __init__(self, hw=DEFAULT_HW, power_mask=POWER_MASK):
        ensure_system_clock()
        self.hw = tuple(hw)
        self._clock_pin = _mask_to_pin(self.hw[5])
        self._data_pin = _mask_to_pin(self.hw[6])
        self._power_pin = _mask_to_pin(power_mask)
        self._clock_mask = self.hw[5]
        self._data_mask = self.hw[6]
        self._power_mask = power_mask
        self._clock = machine.Pin(self._clock_pin, machine.Pin.OUT, value=0)
        self._data = machine.Pin(self._data_pin, machine.Pin.IN)
        self._power = machine.Pin(self._power_pin, machine.Pin.IN)
        self._power_enabled = False
        self.release()

    def pins(self):
        return (
            ("SBWTCK", self._clock_pin),
            ("SBWTDIO", self._data_pin),
            ("VCC", self._power_pin),
        )

    def release(self):
        self._data.init(machine.Pin.IN)
        self._clock.init(machine.Pin.OUT, value=0)

    def power_on(self):
        self.release()
        machine.mem32[GPIO_OUT_SET_ADDR] = self._power_mask
        machine.mem32[GPIO_OE_SET_ADDR] = self._power_mask
        self._power_enabled = True
        time.sleep_ms(SBW_TARGET_POWER_SETTLE_MS)

    def power_off(self):
        self.release()
        machine.mem32[GPIO_OE_CLR_ADDR] = self._power_mask
        self._power.init(machine.Pin.IN)
        self._power_enabled = False

    def status(self):
        gpio_out = machine.mem32[GPIO_OUT_ADDR]
        gpio_oe = machine.mem32[GPIO_OE_ADDR]
        return {
            "power": self._power_enabled,
            "data_driving": bool(gpio_oe & self._data_mask),
            "clock_high": bool(gpio_out & self._clock_mask),
        }

    def read_id(self):
        return sbw_native.read_id(self.hw)

    def bypass_test(self):
        return sbw_native.bypass_test(self.hw)

    def sync_and_por(self):
        return sbw_native.sync_and_por(self.hw)

    def read_mem16(self, address):
        return sbw_native.read_mem16(self.hw, address)

    def write_mem16(self, address, value):
        return sbw_native.write_mem16(self.hw, address, value)

    def read_block16(self, address, words):
        # Returns (ok, raw_bytes) from the native block-read path.
        return sbw_native.read_block16(self.hw, address, words)

    def read_bytes(self, address, length):
        if length < 0:
            raise ValueError("length must be non-negative")
        if length == 0:
            return True, b""

        start = address & ~0x1
        end = address + length
        aligned_end = (end + 1) & ~0x1
        word_count = (aligned_end - start) // 2

        ok, payload = self.read_block16(start, word_count)
        if not ok:
            return False, b""

        offset = address - start
        return True, payload[offset : offset + length]

    def write_block16(self, address, data):
        # Block writes must stay within one writable region class:
        # RAM/peripheral, info FRAM, or main FRAM.
        return bool(sbw_native.write_block16(self.hw, address, data))

    def write_bytes(self, address, data):
        if not data:
            return True

        start = address & ~0x1
        end = address + len(data)
        aligned_end = (end + 1) & ~0x1
        total_length = aligned_end - start
        offset = address - start

        if start != address or total_length != len(data):
            ok, existing = self.read_block16(start, total_length // 2)
            if not ok:
                return False
            payload = bytearray(existing)
        else:
            payload = bytearray(total_length)

        payload[offset : offset + len(data)] = data
        return self.write_block16(start, bytes(payload))

    def verify_bytes(self, address, expected):
        ok, actual = self.read_bytes(address, len(expected))
        return ok and actual == expected, actual

    def mem_smoke16(self, address, value):
        ok, original = self.read_mem16(address)
        if not ok:
            return False, 0, 0, 0

        ok, test_readback = self.write_mem16(address, value)
        if not ok or test_readback != (value & 0xFFFF):
            return False, original, test_readback, 0

        ok, restored_readback = self.write_mem16(address, original)
        if not ok or restored_readback != original:
            return False, original, test_readback, restored_readback

        return True, original, test_readback, restored_readback


def format_status(status):
    return "power=%s data=%s clock=%s" % (
        "on" if status["power"] else "off",
        "driving" if status["data_driving"] else "input",
        "high" if status["clock_high"] else "low",
    )


def format_bypass(ok, captured):
    return "bypass pattern=0x%04X captured=0x%04X expected=0x%04X %s" % (
        BYPASS_PATTERN,
        captured,
        BYPASS_EXPECTED,
        "(expected)" if ok else "(unexpected)",
    )


def format_sync(ok, control_capture):
    state = "(full-emulation)" if ok and (control_capture & FULL_EMULATION_MASK) == FULL_EMULATION_MASK else "(unexpected)"
    return "cntrl-sig=0x%04X %s" % (control_capture, state)
