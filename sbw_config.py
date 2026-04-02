from micropython import const

import machine


SBW_SYS_CLK_HZ = const(150_000_000)

SBW_PIN_CLOCK = const(2)
SBW_PIN_DATA = const(3)
SBW_PIN_TARGET_POWER = const(4)

SBW_TARGET_POWER_SETTLE_MS = const(20)

REGRESSION_DESCRIPTOR_ADDR_0 = const(0x1A00)
REGRESSION_DESCRIPTOR_EXPECTED_0 = const(0x0606)
REGRESSION_DESCRIPTOR_ADDR_1 = const(0x1A14)
REGRESSION_DESCRIPTOR_EXPECTED_1 = const(0x0811)
REGRESSION_RAM_ADDR_0 = const(0x2000)
REGRESSION_RAM_VALUE_0 = const(0x1234)
REGRESSION_RAM_ADDR_1 = const(0x2002)
REGRESSION_RAM_VALUE_1 = const(0xA55A)
REGRESSION_FRAM_ADDR = const(0xC400)
REGRESSION_FRAM_VALUE = const(0x1234)


def current_sys_clk_hz():
    freq = machine.freq()
    if isinstance(freq, tuple):
        return int(freq[0])
    return int(freq)


def ensure_system_clock():
    machine.freq(SBW_SYS_CLK_HZ)
    actual = current_sys_clk_hz()
    if actual != SBW_SYS_CLK_HZ:
        raise RuntimeError("expected 150000000 Hz system clock, got %d Hz" % actual)
    return actual


def bytes_to_words_le(data):
    words = []
    for index in range(0, len(data), 2):
        words.append(data[index] | (data[index + 1] << 8))
    return tuple(words)
