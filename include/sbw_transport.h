#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t clock_low_us;
    uint32_t clock_high_us;
    uint32_t sample_delay_us;
} sbw_timing_t;

typedef enum {
    SBW_ENTRY_RST_HIGH = 0,
    SBW_ENTRY_RST_LOW = 1,
} sbw_entry_mode_t;

void sbw_transport_init(void);
void sbw_transport_start(void);
void sbw_transport_start_mode(sbw_entry_mode_t mode);
void sbw_transport_release(void);

void sbw_transport_clock_test(uint32_t cycles, uint32_t low_us, uint32_t high_us);
bool sbw_transport_io_bit(bool tms, bool tdi, const sbw_timing_t *timing);
void sbw_transport_tclk_set(bool high, const sbw_timing_t *timing);
bool sbw_transport_tclk_is_high(void);
