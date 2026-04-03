import time

import sbw_native
from sbw import SBW
from target_power import TargetPower
from sbw_config import (
    REGRESSION_DESCRIPTOR_ADDR_0,
    REGRESSION_DESCRIPTOR_ADDR_1,
    REGRESSION_DESCRIPTOR_EXPECTED_0,
    REGRESSION_DESCRIPTOR_EXPECTED_1,
    REGRESSION_FRAM_ADDR,
    REGRESSION_FRAM_VALUE,
    REGRESSION_RAM_ADDR_0,
    REGRESSION_RAM_ADDR_1,
    REGRESSION_RAM_VALUE_0,
    REGRESSION_RAM_VALUE_1,
    bytes_to_words_le,
)

# Pico Pin | GPIO | Target Pin
# ---------|------|----------
#       31 | GP26 | SBWTCK
#       32 | GP27 | SBWTDIO
#       33 | GND  | GND
#       34 | GP28 | VCC
SBW_PIN_CLOCK = 26
SBW_PIN_DATA = 27
SBW_PIN_POWER = 28
POWER_SETTLE_MS = 20


def _pattern_word(index):
    return (0xA55A ^ ((index & 0xFFFF) * 0x1357) ^ (index >> 4)) & 0xFFFF


def _pattern_bytes(words):
    payload = bytearray(words * 2)
    for index in range(words):
        value = _pattern_word(index)
        offset = index * 2
        payload[offset] = value & 0xFF
        payload[offset + 1] = (value >> 8) & 0xFF
    return bytes(payload)


def _invert_bytes(payload):
    inverted = bytearray(len(payload))
    for index, value in enumerate(payload):
        inverted[index] = value ^ 0xFF
    return bytes(inverted)


def _bench_write_read_cycle(sbw, address, payload):
    words = len(payload) // 2

    start = time.ticks_us()
    ok = sbw.write_block16(address, payload)
    write_us = time.ticks_diff(time.ticks_us(), start)
    assert ok

    start = time.ticks_us()
    ok, readback = sbw.read_block16(address, words)
    read_us = time.ticks_diff(time.ticks_us(), start)
    assert ok
    assert readback == payload

    return write_us, read_us


def bench_block_roundtrip(sbw, address, words):
    assert words > 0

    first = _pattern_bytes(words)
    write1_us, read1_us = _bench_write_read_cycle(sbw, address, first)

    second = _invert_bytes(first)
    write2_us, read2_us = _bench_write_read_cycle(sbw, address, second)

    return True, (words, write1_us, read1_us, write2_us, read2_us)


def _exercise_byte_io(sbw, address, payload):
    ok, original = sbw.read_bytes(address - 1, len(payload) + 1)
    assert ok

    ok = sbw.write_bytes(address, payload)
    assert ok

    ok, actual = sbw.read_bytes(address, len(payload))
    assert ok and actual == payload

    ok = sbw.write_bytes(address - 1, original)
    assert ok


def run_regression():
    power = TargetPower(SBW_PIN_POWER, POWER_SETTLE_MS)
    sbw = SBW(SBW_PIN_CLOCK, SBW_PIN_DATA)
    power.on()
    try:
        ok, jtag_id = sbw.read_id()
        assert ok and jtag_id == sbw_native.JTAG_ID_EXPECTED

        ok, bypass = sbw.bypass_test()
        assert ok and bypass == sbw_native.BYPASS_EXPECTED

        ok, control_capture = sbw.sync_and_por()
        assert ok and (control_capture & sbw_native.FULL_EMULATION_MASK) == sbw_native.FULL_EMULATION_MASK

        ok, readback = sbw.write_mem16(REGRESSION_RAM_ADDR_0, REGRESSION_RAM_VALUE_0)
        assert ok and readback == REGRESSION_RAM_VALUE_0

        ok, readback = sbw.write_mem16(REGRESSION_RAM_ADDR_1, REGRESSION_RAM_VALUE_1)
        assert ok and readback == REGRESSION_RAM_VALUE_1

        ok, value = sbw.read_mem16(REGRESSION_RAM_ADDR_0)
        assert ok and value == REGRESSION_RAM_VALUE_0

        ok, value = sbw.read_mem16(REGRESSION_RAM_ADDR_1)
        assert ok and value == REGRESSION_RAM_VALUE_1

        descriptor_words = ((REGRESSION_DESCRIPTOR_ADDR_1 - REGRESSION_DESCRIPTOR_ADDR_0) // 2) + 1
        ok, block = sbw.read_block16(REGRESSION_DESCRIPTOR_ADDR_0, descriptor_words)
        descriptor = bytes_to_words_le(block)
        assert ok and descriptor[0] == REGRESSION_DESCRIPTOR_EXPECTED_0
        assert descriptor[-1] == REGRESSION_DESCRIPTOR_EXPECTED_1

        _exercise_byte_io(sbw, REGRESSION_RAM_ADDR_0 + 1, b"\x11\x22\x33\x44\x55\x66\x77")

        ok, original, test_readback, restored_readback = sbw.mem_smoke16(REGRESSION_FRAM_ADDR, REGRESSION_FRAM_VALUE)
        assert ok
        assert test_readback == REGRESSION_FRAM_VALUE
        assert restored_readback == original

        return True
    finally:
        power.off()


def run_bench(words=5120, address=REGRESSION_FRAM_ADDR):
    power = TargetPower(SBW_PIN_POWER, POWER_SETTLE_MS)
    sbw = SBW(SBW_PIN_CLOCK, SBW_PIN_DATA)
    power.on()
    try:
        return bench_block_roundtrip(sbw, address, words)
    finally:
        power.off()
