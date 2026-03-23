#include "sbw_jtag.h"

#include "pico/stdlib.h"

#include "sbw_transport.h"

enum {
    SBW_JTAG_ATTEMPTS = 3,
    SBW_JTAG_TAP_RESET_BITS = 6,
    SBW_JTAG_RETRY_SETTLE_MS = 15,
    SBW_IR_CNTRL_SIG_CAPTURE = 0x14,
    SBW_IR_BYPASS = 0xFF,
};

static const sbw_timing_t k_jtag_timing = {
    .clock_low_us = 5,
    .clock_high_us = 5,
    .sample_delay_us = 2,
};

static bool sbw_jtag_io_bit(bool tms, bool tdi) {
    return sbw_transport_io_bit(tms, tdi, &k_jtag_timing);
}

static void sbw_jtag_begin_session(void) {
    // Match TI's GetCoreID flow: release the lines, let TEST logic settle,
    // re-apply the RST-high SBW entry sequence, then reset the TAP.
    sbw_transport_release();
    sleep_ms(SBW_JTAG_RETRY_SETTLE_MS);
    sbw_transport_start_mode(SBW_ENTRY_RST_HIGH);
    sbw_jtag_tap_reset();
}

static void sbw_jtag_go_to_shift_ir(void) {
    sbw_jtag_io_bit(true, true);
    sbw_jtag_io_bit(true, true);
    sbw_jtag_io_bit(false, true);
    sbw_jtag_io_bit(false, true);
}

static void sbw_jtag_go_to_shift_dr(void) {
    sbw_jtag_io_bit(true, true);
    sbw_jtag_io_bit(false, true);
    sbw_jtag_io_bit(false, true);
}

static void sbw_jtag_finish_shift(void) {
    sbw_jtag_io_bit(true, true);
    sbw_jtag_io_bit(false, true);
}

static uint8_t sbw_jtag_shift_ir8(uint8_t instruction) {
    uint8_t captured = 0;

    sbw_jtag_go_to_shift_ir();

    // MSP430 JTAG instructions are shifted LSB first.
    for (uint32_t bit = 0; bit < 8; ++bit) {
        const bool tms = (bit == 7);
        const bool tdi = ((instruction >> bit) & 0x1u) != 0;
        captured = (uint8_t)((captured << 1) | (sbw_jtag_io_bit(tms, tdi) ? 1u : 0u));
    }

    sbw_jtag_finish_shift();
    return captured;
}

static uint16_t sbw_jtag_shift_dr16(uint16_t data) {
    uint16_t captured = 0;

    sbw_jtag_go_to_shift_dr();

    // MSP430 JTAG data words are shifted MSB first.
    for (uint32_t bit = 0; bit < 16; ++bit) {
        const bool tms = (bit == 15);
        const bool tdi = ((data >> (15 - bit)) & 0x1u) != 0;
        captured = (uint16_t)((captured << 1) | (sbw_jtag_io_bit(tms, tdi) ? 1u : 0u));
    }

    sbw_jtag_finish_shift();
    return captured;
}

void sbw_jtag_tap_reset(void) {
    for (uint32_t bit = 0; bit < SBW_JTAG_TAP_RESET_BITS; ++bit) {
        sbw_jtag_io_bit(true, true);
    }

    sbw_jtag_io_bit(false, true);
}

bool sbw_jtag_read_id(uint8_t *id) {
    uint8_t last_id = 0;

    for (uint32_t attempt = 0; attempt < SBW_JTAG_ATTEMPTS; ++attempt) {
        sbw_jtag_begin_session();
        last_id = sbw_jtag_shift_ir8(SBW_IR_CNTRL_SIG_CAPTURE);
        sbw_transport_release();

        if (last_id == SBW_JTAG_ID_EXPECTED) {
            if (id) {
                *id = last_id;
            }
            return true;
        }
    }

    if (id) {
        *id = last_id;
    }

    return false;
}

bool sbw_jtag_bypass_test(uint16_t *captured) {
    uint16_t last_capture = 0;

    for (uint32_t attempt = 0; attempt < SBW_JTAG_ATTEMPTS; ++attempt) {
        sbw_jtag_begin_session();
        (void)sbw_jtag_shift_ir8(SBW_IR_BYPASS);
        last_capture = sbw_jtag_shift_dr16(SBW_BYPASS_SMOKE_PATTERN);
        sbw_transport_release();

        if (last_capture == SBW_BYPASS_SMOKE_EXPECTED) {
            if (captured) {
                *captured = last_capture;
            }
            return true;
        }
    }

    if (captured) {
        *captured = last_capture;
    }

    return false;
}
