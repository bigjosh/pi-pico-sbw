#pragma once

#include <stdint.h>

enum {
    SBW_PIN_CLOCK = 2,
    SBW_PIN_DATA = 3,
    SBW_PIN_TARGET_POWER = 4,
};

enum {
    SBW_DEFAULT_CLOCK_LOW_US = 2,
    SBW_DEFAULT_CLOCK_HIGH_US = 2,
    SBW_DEFAULT_SAMPLE_DELAY_US = 1,
    SBW_TARGET_POWER_SETTLE_MS = 20,
};
