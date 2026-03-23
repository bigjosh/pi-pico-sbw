#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    SBW_JTAG_ID_EXPECTED = 0x98,
    SBW_BYPASS_SMOKE_PATTERN = 0xA55A,
    SBW_BYPASS_SMOKE_EXPECTED = 0x52AD,
};

bool sbw_jtag_read_id(uint8_t *id);
bool sbw_jtag_bypass_test(uint16_t *captured);
void sbw_jtag_tap_reset(void);
