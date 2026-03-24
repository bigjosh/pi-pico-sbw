#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    SBW_JTAG_ID_EXPECTED = 0x98,
    SBW_BYPASS_SMOKE_PATTERN = 0xA55A,
    SBW_BYPASS_SMOKE_EXPECTED = 0x52AD,
};

typedef struct {
    uint16_t original;
    uint16_t test_readback;
    uint16_t restored_readback;
} sbw_jtag_fram_smoke_result_t;

bool sbw_jtag_read_id(uint8_t *id);
bool sbw_jtag_bypass_test(uint16_t *captured);
bool sbw_jtag_sync_and_por(uint16_t *control_capture);
bool sbw_jtag_read_mem16(uint32_t address, uint16_t *data);
bool sbw_jtag_write_mem16(uint32_t address, uint16_t value, uint16_t *readback);
bool sbw_jtag_fram_smoke16(uint32_t address, uint16_t value, sbw_jtag_fram_smoke_result_t *result);
void sbw_jtag_tap_reset(void);
