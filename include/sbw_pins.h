#pragma once

#include <stdint.h>

enum {
    SBW_PIN_CLOCK = 2,
    SBW_PIN_DATA = 3,
    SBW_PIN_TARGET_POWER = 4,
};

enum {
    SBW_ACTIVE_SLOT_LOW_NS = 100,
    SBW_ACTIVE_SLOT_HIGH_NS = 100,
    SBW_TARGET_POWER_SETTLE_MS = 20,
};
