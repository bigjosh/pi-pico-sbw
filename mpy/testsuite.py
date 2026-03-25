import time

from sbw import SBWNative
from sbw_config import (
    BYPASS_EXPECTED,
    FULL_EMULATION_MASK,
    JTAG_ID_EXPECTED,
    REGRESSION_FRAM_ADDR,
    REGRESSION_FRAM_VALUE,
    REGRESSION_RAM_ADDR_0,
    REGRESSION_RAM_ADDR_1,
    REGRESSION_RAM_VALUE_0,
    REGRESSION_RAM_VALUE_1,
    REGRESSION_READ_ADDR_0,
    REGRESSION_READ_ADDR_1,
    REGRESSION_READ_EXPECTED_0,
    REGRESSION_READ_EXPECTED_1,
    bytes_to_words_le,
)


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


def run_regression():
    sbw = SBWNative()
    sbw.power_on()
    try:
        ok, jtag_id = sbw.read_id()
        assert ok and jtag_id == JTAG_ID_EXPECTED

        ok, bypass = sbw.bypass_test()
        assert ok and bypass == BYPASS_EXPECTED

        ok, control_capture = sbw.sync_and_por()
        assert ok and (control_capture & FULL_EMULATION_MASK) == FULL_EMULATION_MASK

        ok, value = sbw.read_mem16(REGRESSION_READ_ADDR_0)
        assert ok and value == REGRESSION_READ_EXPECTED_0

        ok, value = sbw.read_mem16(REGRESSION_READ_ADDR_1)
        assert ok and value == REGRESSION_READ_EXPECTED_1

        ok, block = sbw.read_block16(REGRESSION_READ_ADDR_1, 2)
        assert ok and bytes_to_words_le(block) == (
            REGRESSION_READ_EXPECTED_1,
            REGRESSION_READ_EXPECTED_0,
        )

        ok, readback = sbw.write_mem16(REGRESSION_RAM_ADDR_0, REGRESSION_RAM_VALUE_0)
        assert ok and readback == REGRESSION_RAM_VALUE_0

        ok, readback = sbw.write_mem16(REGRESSION_RAM_ADDR_1, REGRESSION_RAM_VALUE_1)
        assert ok and readback == REGRESSION_RAM_VALUE_1

        ok, original, test_readback, restored_readback = sbw.fram_smoke16(REGRESSION_FRAM_ADDR, REGRESSION_FRAM_VALUE)
        assert ok
        assert test_readback == REGRESSION_FRAM_VALUE
        assert restored_readback == original

        return True
    finally:
        sbw.power_off()


def run_bench(words=5120, address=REGRESSION_FRAM_ADDR):
    sbw = SBWNative()
    sbw.power_on()
    try:
        return bench_block_roundtrip(sbw, address, words)
    finally:
        sbw.power_off()
