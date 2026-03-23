#include "sbw_transport.h"

#include "hardware/sync.h"
#include "hardware/timer.h"
#include "pico/stdlib.h"

#include "sbw_hw.h"
#include "sbw_pins.h"

enum {
    SBW_EXIT_HOLD_US = 200,
    SBW_ENTRY_RST_HIGH_TEST_RESET_MS = 4,
    SBW_ENTRY_RST_HIGH_TEST_ENABLE_MS = 20,
    SBW_ENTRY_RST_HIGH_SETUP_US = 60,
    SBW_ENTRY_RST_LOW_TEST_RESET_MS = 1,
    SBW_ENTRY_RST_LOW_HOLD_RESET_MS = 50,
    SBW_ENTRY_RST_LOW_TEST_ENABLE_MS = 100,
    SBW_ENTRY_RST_LOW_SETUP_US = 40,
    SBW_ENTRY_PULSE_LOW_US = 1,
    SBW_ENTRY_POST_ENABLE_MS = 5,
};

static bool g_tclk_high = true;

static uint32_t sbw_transport_low_phase_begin(void) {
    const uint32_t irq_state = save_and_disable_interrupts();
    sbw_hw_clock_drive(false);
    return irq_state;
}

static void sbw_transport_low_phase_end(uint32_t irq_state) {
    sbw_hw_clock_drive(true);
    restore_interrupts(irq_state);
}

static void sbw_transport_short_clock_pulse_low(uint32_t low_us) {
    const uint32_t irq_state = sbw_transport_low_phase_begin();
    busy_wait_us_32(low_us);
    sbw_transport_low_phase_end(irq_state);
}

static void sbw_transport_slot_drive(bool level, const sbw_timing_t *timing) {
    sbw_hw_data_drive(level);
    busy_wait_us_32(timing->clock_high_us);
    sbw_transport_short_clock_pulse_low(timing->clock_low_us);
}

static void sbw_transport_slot_tmsldh(const sbw_timing_t *timing) {
    uint32_t low_before_drive_us = timing->clock_low_us / 2;
    if (low_before_drive_us == 0) {
        low_before_drive_us = 1;
    }
    if (low_before_drive_us >= timing->clock_low_us) {
        low_before_drive_us = timing->clock_low_us - 1;
    }

    sbw_hw_data_drive(false);
    busy_wait_us_32(timing->clock_high_us);
    const uint32_t irq_state = sbw_transport_low_phase_begin();
    busy_wait_us_32(low_before_drive_us);
    sbw_hw_data_drive(true);
    busy_wait_us_32(timing->clock_low_us - low_before_drive_us);
    sbw_transport_low_phase_end(irq_state);
}

static bool sbw_transport_slot_tdo(bool sample, const sbw_timing_t *timing) {
    sbw_hw_data_release();
    busy_wait_us_32(timing->clock_high_us);
    const uint32_t irq_state = sbw_transport_low_phase_begin();

    if (!sample) {
        busy_wait_us_32(timing->clock_low_us);
        sbw_transport_low_phase_end(irq_state);
        return false;
    }

    busy_wait_us_32(timing->sample_delay_us);
    const bool tdo = sbw_hw_data_read();

    if (timing->clock_low_us > timing->sample_delay_us) {
        busy_wait_us_32(timing->clock_low_us - timing->sample_delay_us);
    }

    sbw_transport_low_phase_end(irq_state);
    return tdo;
}

static void sbw_transport_entry_rst_high(void) {
    // Match TI's FR4xx/FR2xx SBW entry sequence: reset TEST logic low, raise
    // RST/SBWTDIO, activate TEST, then issue the low-high TEST pulse.
    sbw_hw_clock_drive(false);
    sleep_ms(SBW_ENTRY_RST_HIGH_TEST_RESET_MS);

    sbw_hw_data_drive(true);
    sbw_hw_clock_drive(true);
    sleep_ms(SBW_ENTRY_RST_HIGH_TEST_ENABLE_MS);

    sbw_hw_data_drive(true);
    busy_wait_us_32(SBW_ENTRY_RST_HIGH_SETUP_US);

    sbw_transport_short_clock_pulse_low(SBW_ENTRY_PULSE_LOW_US);
    busy_wait_us_32(SBW_ENTRY_RST_HIGH_SETUP_US);

    sleep_ms(SBW_ENTRY_POST_ENABLE_MS);
}

static void sbw_transport_entry_rst_low(void) {
    sbw_hw_clock_drive(false);
    sleep_ms(SBW_ENTRY_RST_LOW_TEST_RESET_MS);

    sbw_hw_data_drive(false);
    sleep_ms(SBW_ENTRY_RST_LOW_HOLD_RESET_MS);

    sbw_hw_clock_drive(true);
    sleep_ms(SBW_ENTRY_RST_LOW_TEST_ENABLE_MS);

    sbw_hw_data_drive(true);
    busy_wait_us_32(SBW_ENTRY_RST_LOW_SETUP_US);

    sbw_transport_short_clock_pulse_low(SBW_ENTRY_PULSE_LOW_US);
    busy_wait_us_32(SBW_ENTRY_RST_LOW_SETUP_US);

    sleep_ms(SBW_ENTRY_POST_ENABLE_MS);
}

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
    g_tclk_high = true;
    sbw_transport_release();
}

void sbw_transport_start(void) {
    sbw_transport_start_mode(SBW_ENTRY_RST_HIGH);
}

void sbw_transport_start_mode(sbw_entry_mode_t mode) {
    sbw_transport_release();
    if (mode == SBW_ENTRY_RST_LOW) {
        sbw_transport_entry_rst_low();
        g_tclk_high = true;
        return;
    }

    sbw_transport_entry_rst_high();
    g_tclk_high = true;
}

void sbw_transport_release(void) {
    g_tclk_high = true;
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
        sbw_transport_short_clock_pulse_low(low_us);
        busy_wait_us_32(high_us);
    }
}

bool sbw_transport_io_bit(bool tms, bool tdi, const sbw_timing_t *timing) {
    const sbw_timing_t active = sbw_normalize_timing(timing);

    // Each logical JTAG bit is encoded as TMS, TDI, then a released TDO slot.
    sbw_transport_slot_drive(tms, &active);
    sbw_transport_slot_drive(tdi, &active);
    return sbw_transport_slot_tdo(true, &active);
}

void sbw_transport_tclk_set(bool high, const sbw_timing_t *timing) {
    const sbw_timing_t active = sbw_normalize_timing(timing);

    if (g_tclk_high) {
        sbw_transport_slot_tmsldh(&active);
    } else {
        sbw_transport_slot_drive(false, &active);
    }

    sbw_transport_slot_drive(high, &active);
    (void)sbw_transport_slot_tdo(false, &active);
    g_tclk_high = high;
}

bool sbw_transport_tclk_is_high(void) {
    return g_tclk_high;
}
