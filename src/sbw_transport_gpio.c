#include "sbw_transport.h"

#include "hardware/timer.h"

#include "sbw_hw.h"
#include "sbw_pins.h"

enum {
    SBW_EXIT_HOLD_US = 200,
    SBW_ENTRY_SETUP_US = 5,
    SBW_ENTRY_ENABLE_US = 120,
    SBW_ENTRY_LOW_US = 5,
    SBW_ENTRY_HIGH_US = 5,
};

static sbw_timing_t sbw_normalize_timing(const sbw_timing_t *timing) {
    sbw_timing_t normalized = {
        .clock_low_us = SBW_DEFAULT_CLOCK_LOW_US,
        .clock_high_us = SBW_DEFAULT_CLOCK_HIGH_US,
        .sample_delay_us = SBW_DEFAULT_SAMPLE_DELAY_US,
    };

    if (!timing) {
        return normalized;
    }

    if (timing->clock_low_us > 0) {
        normalized.clock_low_us = timing->clock_low_us;
    }

    if (timing->clock_high_us > 0) {
        normalized.clock_high_us = timing->clock_high_us;
    }

    if (timing->sample_delay_us > 0) {
        normalized.sample_delay_us = timing->sample_delay_us;
    }

    if (normalized.sample_delay_us >= normalized.clock_low_us) {
        normalized.sample_delay_us = normalized.clock_low_us / 2;
        if (normalized.sample_delay_us == 0) {
            normalized.sample_delay_us = 1;
            normalized.clock_low_us = 2;
        }
    }

    return normalized;
}

void sbw_transport_init(void) {
    sbw_transport_release();
}

void sbw_transport_start(void) {
    sbw_transport_start_mode(SBW_ENTRY_RST_HIGH);
}

void sbw_transport_start_mode(sbw_entry_mode_t mode) {
    sbw_transport_release();

    // Enter SBW using the timing window from the FR4133 datasheet and the
    // entry waveforms from SLAU320 Figure 2-13.
    sbw_hw_data_drive(mode == SBW_ENTRY_RST_HIGH);
    busy_wait_us_32(SBW_ENTRY_SETUP_US);

    sbw_hw_clock_drive(true);
    busy_wait_us_32(SBW_ENTRY_ENABLE_US);

    sbw_hw_clock_drive(false);
    if (mode == SBW_ENTRY_RST_LOW) {
        sbw_hw_data_drive(true);
    }

    busy_wait_us_32(SBW_ENTRY_LOW_US);

    sbw_hw_clock_drive(true);
    busy_wait_us_32(SBW_ENTRY_HIGH_US);
}

void sbw_transport_release(void) {
    sbw_hw_data_release();
    sbw_hw_clock_drive(false);
    busy_wait_us_32(SBW_EXIT_HOLD_US);
}

void sbw_transport_clock_test(uint32_t cycles, uint32_t low_us, uint32_t high_us) {
    if (cycles == 0) {
        return;
    }

    if (low_us == 0) {
        low_us = SBW_DEFAULT_CLOCK_LOW_US;
    }

    if (high_us == 0) {
        high_us = SBW_DEFAULT_CLOCK_HIGH_US;
    }

    sbw_transport_release();

    for (uint32_t i = 0; i < cycles; ++i) {
        sbw_hw_clock_pulse_us(low_us, high_us);
    }
}

bool sbw_transport_io_bit(bool tms, bool tdi, const sbw_timing_t *timing) {
    const sbw_timing_t active = sbw_normalize_timing(timing);

    // Each logical JTAG bit is encoded as TMS, TDI, then a released TDO slot.
    sbw_hw_data_drive(tms);
    busy_wait_us_32(active.clock_high_us);
    sbw_hw_clock_drive(false);
    busy_wait_us_32(active.clock_low_us);
    sbw_hw_clock_drive(true);

    sbw_hw_data_drive(tdi);
    busy_wait_us_32(active.clock_high_us);
    sbw_hw_clock_drive(false);
    busy_wait_us_32(active.clock_low_us);
    sbw_hw_clock_drive(true);

    sbw_hw_data_release();
    busy_wait_us_32(active.clock_high_us);

    sbw_hw_clock_drive(false);
    busy_wait_us_32(active.sample_delay_us);
    const bool tdo = sbw_hw_data_read();

    if (active.clock_low_us > active.sample_delay_us) {
        busy_wait_us_32(active.clock_low_us - active.sample_delay_us);
    }

    sbw_hw_clock_drive(true);

    return tdo;
}
