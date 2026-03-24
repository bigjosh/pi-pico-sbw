#include "sbw_transport.h"

#include "hardware/sync.h"
#include "pico/stdlib.h"

#include "sbw_hw.h"
#include "sbw_pins.h"

enum {
    SBW_EXIT_HOLD_NS = 200000,
    SBW_ENTRY_RST_HIGH_TEST_RESET_MS = 4,
    SBW_ENTRY_RST_HIGH_TEST_ENABLE_MS = 20,
    SBW_ENTRY_RST_HIGH_SETUP_NS = 60000,
    SBW_ENTRY_RST_LOW_TEST_RESET_MS = 1,
    SBW_ENTRY_RST_LOW_HOLD_RESET_MS = 50,
    SBW_ENTRY_RST_LOW_TEST_ENABLE_MS = 100,
    SBW_ENTRY_RST_LOW_SETUP_NS = 40000,
    SBW_ENTRY_PULSE_LOW_NS = 1000,
    SBW_ENTRY_POST_ENABLE_MS = 5,
};

#define SBW_NS_TO_CYCLES(delay_ns) \
    ((uint32_t)((((uint64_t)SYS_CLK_HZ) * (uint64_t)(delay_ns) + 999999999ull) / 1000000000ull))

enum {
    SBW_EXIT_HOLD_CYCLES = SBW_NS_TO_CYCLES(SBW_EXIT_HOLD_NS),
    SBW_ENTRY_RST_HIGH_SETUP_CYCLES = SBW_NS_TO_CYCLES(SBW_ENTRY_RST_HIGH_SETUP_NS),
    SBW_ENTRY_RST_LOW_SETUP_CYCLES = SBW_NS_TO_CYCLES(SBW_ENTRY_RST_LOW_SETUP_NS),
    SBW_ENTRY_PULSE_LOW_CYCLES = SBW_NS_TO_CYCLES(SBW_ENTRY_PULSE_LOW_NS),
    SBW_ACTIVE_SLOT_LOW_CYCLES = SBW_NS_TO_CYCLES(SBW_ACTIVE_SLOT_LOW_NS),
    SBW_ACTIVE_SLOT_HIGH_CYCLES = SBW_NS_TO_CYCLES(SBW_ACTIVE_SLOT_HIGH_NS),
};

static bool g_tclk_high = true;

static void sbw_transport_wait_cycles(uint32_t cycles) {
    if (cycles == 0) {
        return;
    }

    busy_wait_at_least_cycles(cycles);
}

static uint32_t sbw_transport_low_phase_begin(void) {
    const uint32_t irq_state = save_and_disable_interrupts();
    sbw_hw_clock_drive(false);
    return irq_state;
}

static void sbw_transport_low_phase_end(uint32_t irq_state) {
    sbw_hw_clock_drive(true);
    restore_interrupts(irq_state);
}

static void sbw_transport_pulse_low_cycles(uint32_t low_cycles) {
    const uint32_t irq_state = sbw_transport_low_phase_begin();
    sbw_transport_wait_cycles(low_cycles);
    sbw_transport_low_phase_end(irq_state);
}

static void sbw_transport_short_clock_pulse_low(void) {
    sbw_transport_pulse_low_cycles(SBW_ACTIVE_SLOT_LOW_CYCLES);
}

static void sbw_transport_slot_drive(bool level) {
    sbw_hw_data_drive(level);
    sbw_transport_wait_cycles(SBW_ACTIVE_SLOT_HIGH_CYCLES);
    sbw_transport_short_clock_pulse_low();
}

static void sbw_transport_slot_tmsldh(void) {
    uint32_t low_before_drive_cycles = SBW_ACTIVE_SLOT_LOW_CYCLES / 2;
    if (SBW_ACTIVE_SLOT_LOW_CYCLES > 1) {
        if (low_before_drive_cycles == 0) {
            low_before_drive_cycles = 1;
        }
        if (low_before_drive_cycles >= SBW_ACTIVE_SLOT_LOW_CYCLES) {
            low_before_drive_cycles = SBW_ACTIVE_SLOT_LOW_CYCLES - 1;
        }
    }

    sbw_hw_data_drive(false);
    sbw_transport_wait_cycles(SBW_ACTIVE_SLOT_HIGH_CYCLES);
    const uint32_t irq_state = sbw_transport_low_phase_begin();
    sbw_transport_wait_cycles(low_before_drive_cycles);
    sbw_hw_data_drive(true);
    sbw_transport_wait_cycles(SBW_ACTIVE_SLOT_LOW_CYCLES - low_before_drive_cycles);
    sbw_transport_low_phase_end(irq_state);
}

static bool sbw_transport_slot_tdo(bool sample) {
    sbw_hw_data_release();
    sbw_transport_wait_cycles(SBW_ACTIVE_SLOT_HIGH_CYCLES);
    const uint32_t irq_state = sbw_transport_low_phase_begin();
    sbw_transport_wait_cycles(SBW_ACTIVE_SLOT_LOW_CYCLES);
    const bool tdo = sample ? sbw_hw_data_read() : false;

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
    sbw_transport_wait_cycles(SBW_ENTRY_RST_HIGH_SETUP_CYCLES);

    sbw_transport_pulse_low_cycles(SBW_ENTRY_PULSE_LOW_CYCLES);
    sbw_transport_wait_cycles(SBW_ENTRY_RST_HIGH_SETUP_CYCLES);

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
    sbw_transport_wait_cycles(SBW_ENTRY_RST_LOW_SETUP_CYCLES);

    sbw_transport_pulse_low_cycles(SBW_ENTRY_PULSE_LOW_CYCLES);
    sbw_transport_wait_cycles(SBW_ENTRY_RST_LOW_SETUP_CYCLES);

    sleep_ms(SBW_ENTRY_POST_ENABLE_MS);
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
    sbw_transport_wait_cycles(SBW_EXIT_HOLD_CYCLES);
}

void sbw_transport_clock_test(uint32_t cycles) {
    if (cycles == 0) {
        return;
    }

    sbw_transport_release();

    for (uint32_t i = 0; i < cycles; ++i) {
        sbw_transport_short_clock_pulse_low();
        sbw_transport_wait_cycles(SBW_ACTIVE_SLOT_HIGH_CYCLES);
    }
}

bool sbw_transport_io_bit(bool tms, bool tdi) {
    // Each logical JTAG bit is encoded as TMS, TDI, then a released TDO slot.
    sbw_transport_slot_drive(tms);
    sbw_transport_slot_drive(tdi);
    return sbw_transport_slot_tdo(true);
}

void sbw_transport_tclk_set(bool high) {
    if (g_tclk_high) {
        sbw_transport_slot_tmsldh();
    } else {
        sbw_transport_slot_drive(false);
    }

    sbw_transport_slot_drive(high);
    (void)sbw_transport_slot_tdo(false);
    g_tclk_high = high;
}

bool sbw_transport_tclk_is_high(void) {
    return g_tclk_high;
}
