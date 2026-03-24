#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SBW_ENTRY_RST_HIGH = 0,
    SBW_ENTRY_RST_LOW = 1,
} sbw_entry_mode_t;

void sbw_transport_init(void);
void sbw_transport_start(void);
void sbw_transport_start_mode(sbw_entry_mode_t mode);
void sbw_transport_release(void);

void sbw_transport_clock_test(uint32_t cycles);
bool sbw_transport_io_bit(bool tms, bool tdi);
void sbw_transport_tclk_set(bool high);
bool sbw_transport_tclk_is_high(void);
