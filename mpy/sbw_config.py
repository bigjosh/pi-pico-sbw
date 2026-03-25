from micropython import const

import machine


SBW_SYS_CLK_HZ = const(150_000_000)

SBW_PIN_CLOCK = const(2)
SBW_PIN_DATA = const(3)
SBW_PIN_TARGET_POWER = const(4)

SBW_TARGET_POWER_SETTLE_MS = const(20)

SIO_BASE = const(0xD000_0000)
GPIO_OUT_ADDR = const(SIO_BASE + 0x10)
GPIO_OUT_SET_ADDR = const(SIO_BASE + 0x18)
GPIO_OUT_CLR_ADDR = const(SIO_BASE + 0x20)
GPIO_OE_ADDR = const(SIO_BASE + 0x30)
GPIO_OE_SET_ADDR = const(SIO_BASE + 0x38)
GPIO_OE_CLR_ADDR = const(SIO_BASE + 0x40)
GPIO_IN_ADDR = const(SIO_BASE + 0x04)

CLOCK_MASK = const(1 << SBW_PIN_CLOCK)
DATA_MASK = const(1 << SBW_PIN_DATA)
POWER_MASK = const(1 << SBW_PIN_TARGET_POWER)

HW_TUPLE_LAYOUT = (
    "gpio_in_addr",
    "gpio_out_set_addr",
    "gpio_out_clr_addr",
    "gpio_oe_set_addr",
    "gpio_oe_clr_addr",
    "clock_mask",
    "data_mask",
    "power_mask",
)

DEFAULT_HW = (
    GPIO_IN_ADDR,
    GPIO_OUT_SET_ADDR,
    GPIO_OUT_CLR_ADDR,
    GPIO_OE_SET_ADDR,
    GPIO_OE_CLR_ADDR,
    CLOCK_MASK,
    DATA_MASK,
    POWER_MASK,
)

REGRESSION_READ_ADDR_0 = const(0xFFFE)
REGRESSION_READ_EXPECTED_0 = const(0xEDC8)
REGRESSION_READ_ADDR_1 = const(0xFFFC)
REGRESSION_READ_EXPECTED_1 = const(0xEE44)
REGRESSION_RAM_ADDR_0 = const(0x2000)
REGRESSION_RAM_VALUE_0 = const(0x1234)
REGRESSION_RAM_ADDR_1 = const(0x2002)
REGRESSION_RAM_VALUE_1 = const(0xA55A)
REGRESSION_FRAM_ADDR = const(0xC400)
REGRESSION_FRAM_VALUE = const(0x1234)

JTAG_ID_EXPECTED = const(0x98)
BYPASS_PATTERN = const(0xA55A)
BYPASS_EXPECTED = const(0x52AD)
FULL_EMULATION_MASK = const(0x0301)


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


def cycles_to_us(cycles):
    return int(cycles) // 150


def bytes_to_words_le(data):
    words = []
    for index in range(0, len(data), 2):
        words.append(data[index] | (data[index + 1] << 8))
    return tuple(words)
