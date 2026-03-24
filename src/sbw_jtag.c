#include "sbw_jtag.h"

#include "pico/stdlib.h"

#include "sbw_transport.h"

enum {
    SBW_JTAG_ATTEMPTS = 3,
    SBW_JTAG_TAP_RESET_BITS = 6,
    SBW_JTAG_RETRY_SETTLE_MS = 15,
    SBW_JTAG_SYNC_RETRIES = 50,
    SBW_JTAG_SYNC_MASK = 0x0200,
    SBW_JTAG_FULL_EMULATION_MASK = 0x0301,
    SBW_IR_CNTRL_SIG_16BIT = 0x13,
    SBW_IR_CNTRL_SIG_CAPTURE = 0x14,
    SBW_IR_DATA_16BIT = 0x41,
    SBW_IR_DATA_CAPTURE = 0x42,
    SBW_IR_ADDR_16BIT = 0x83,
    SBW_IR_DATA_TO_ADDR = 0x85,
    SBW_IR_BYPASS = 0xFF,
    SBW_SAFE_FRAM_PC = 0x0004,
    SBW_WDTCTL_ADDR_FR4XX = 0x01CC,
    SBW_WDTCTL_HOLD = 0x5A80,
};

static bool sbw_jtag_io_bit(bool tms, bool tdi) {
    return sbw_transport_io_bit(tms, tdi);
}

static void sbw_jtag_tclk_set(bool high) {
    sbw_transport_tclk_set(high);
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
    sbw_jtag_io_bit(true, sbw_transport_tclk_is_high());
    sbw_jtag_io_bit(true, true);
    sbw_jtag_io_bit(false, true);
    sbw_jtag_io_bit(false, true);
}

static void sbw_jtag_go_to_shift_dr(void) {
    sbw_jtag_io_bit(true, sbw_transport_tclk_is_high());
    sbw_jtag_io_bit(false, true);
    sbw_jtag_io_bit(false, true);
}

static void sbw_jtag_finish_shift(void) {
    sbw_jtag_io_bit(true, true);
    sbw_jtag_io_bit(false, sbw_transport_tclk_is_high());
}

static uint8_t sbw_jtag_shift_ir8(uint8_t instruction) {
    uint8_t captured = 0;
    uint8_t shift = instruction;

    sbw_jtag_go_to_shift_ir();

    // MSP430 JTAG instructions are shifted LSB first.
    for (uint32_t bit = 0; bit < 7; ++bit) {
        const bool tdi = (shift & 0x1u) != 0;
        captured = (uint8_t)((captured << 1) | (sbw_jtag_io_bit(false, tdi) ? 1u : 0u));
        shift >>= 1;
    }
    captured = (uint8_t)((captured << 1) | (sbw_jtag_io_bit(true, (shift & 0x1u) != 0) ? 1u : 0u));

    sbw_jtag_finish_shift();
    return captured;
}

static uint16_t sbw_jtag_shift_dr16(uint16_t data) {
    uint16_t captured = 0;
    uint16_t shift = data;

    sbw_jtag_go_to_shift_dr();

    // MSP430 JTAG data words are shifted MSB first.
    for (uint32_t bit = 0; bit < 15; ++bit) {
        const bool tdi = (shift & 0x8000u) != 0;
        captured = (uint16_t)((captured << 1) | (sbw_jtag_io_bit(false, tdi) ? 1u : 0u));
        shift <<= 1;
    }
    captured = (uint16_t)((captured << 1) | (sbw_jtag_io_bit(true, (shift & 0x8000u) != 0) ? 1u : 0u));

    sbw_jtag_finish_shift();
    return captured;
}

static uint32_t sbw_jtag_shift_dr20(uint32_t data) {
    uint32_t captured = 0;
    uint32_t shift = data & 0x000FFFFFu;

    sbw_jtag_go_to_shift_dr();

    for (uint32_t bit = 0; bit < 19; ++bit) {
        const bool tdi = (shift & 0x00080000u) != 0;
        captured = (captured << 1) | (sbw_jtag_io_bit(false, tdi) ? 1u : 0u);
        shift <<= 1;
    }
    captured = (captured << 1) | (sbw_jtag_io_bit(true, (shift & 0x00080000u) != 0) ? 1u : 0u);

    sbw_jtag_finish_shift();
    return captured & 0x000FFFFFu;
}

static uint16_t sbw_jtag_read_control_signal(void) {
    (void)sbw_jtag_shift_ir8(SBW_IR_CNTRL_SIG_CAPTURE);
    return sbw_jtag_shift_dr16(0x0000);
}

static bool sbw_jtag_in_full_emulation(uint16_t *control_capture) {
    const uint16_t status = sbw_jtag_read_control_signal();

    if (control_capture) {
        *control_capture = status;
    }

    return (status & SBW_JTAG_FULL_EMULATION_MASK) == SBW_JTAG_FULL_EMULATION_MASK;
}

static bool sbw_jtag_sync_cpu(void) {
    (void)sbw_jtag_shift_ir8(SBW_IR_CNTRL_SIG_16BIT);
    (void)sbw_jtag_shift_dr16(0x1501);

    if (sbw_jtag_shift_ir8(SBW_IR_CNTRL_SIG_CAPTURE) != SBW_JTAG_ID_EXPECTED) {
        return false;
    }

    for (uint32_t attempt = 0; attempt < SBW_JTAG_SYNC_RETRIES; ++attempt) {
        if ((sbw_jtag_shift_dr16(0x0000) & SBW_JTAG_SYNC_MASK) != 0) {
            return true;
        }
    }

    return false;
}

static bool sbw_jtag_execute_por(uint16_t *control_capture) {
    // Follow TI's ExecutePOR_430Xv2 flow through the point where the target
    // should be back in Full-Emulation-State.
    sbw_jtag_tclk_set(false);
    sbw_jtag_tclk_set(true);

    (void)sbw_jtag_shift_ir8(SBW_IR_CNTRL_SIG_16BIT);
    (void)sbw_jtag_shift_dr16(0x0C01);
    (void)sbw_jtag_shift_dr16(0x0401);

    (void)sbw_jtag_shift_ir8(SBW_IR_DATA_16BIT);
    sbw_jtag_tclk_set(false);
    sbw_jtag_tclk_set(true);
    sbw_jtag_tclk_set(false);
    sbw_jtag_tclk_set(true);
    (void)sbw_jtag_shift_dr16(SBW_SAFE_FRAM_PC);

    sbw_jtag_tclk_set(false);
    sbw_jtag_tclk_set(true);

    (void)sbw_jtag_shift_ir8(SBW_IR_DATA_CAPTURE);

    sbw_jtag_tclk_set(false);
    sbw_jtag_tclk_set(true);
    sbw_jtag_tclk_set(false);
    sbw_jtag_tclk_set(true);

    (void)sbw_jtag_shift_ir8(SBW_IR_CNTRL_SIG_16BIT);
    (void)sbw_jtag_shift_dr16(0x0501);
    sbw_jtag_tclk_set(false);
    sbw_jtag_tclk_set(true);

    (void)sbw_jtag_shift_ir8(SBW_IR_CNTRL_SIG_CAPTURE);
    const uint16_t status = sbw_jtag_shift_dr16(0x0000);

    if (control_capture) {
        *control_capture = status;
    }

    return (status & SBW_JTAG_FULL_EMULATION_MASK) == SBW_JTAG_FULL_EMULATION_MASK;
}

static bool sbw_jtag_write_mem16_internal(uint32_t address, uint16_t data) {
    if (!sbw_jtag_in_full_emulation(NULL)) {
        return false;
    }

    sbw_jtag_tclk_set(false);
    (void)sbw_jtag_shift_ir8(SBW_IR_CNTRL_SIG_16BIT);
    (void)sbw_jtag_shift_dr16(0x0500);
    (void)sbw_jtag_shift_ir8(SBW_IR_ADDR_16BIT);
    (void)sbw_jtag_shift_dr20(address & 0x000FFFFFu);

    sbw_jtag_tclk_set(true);
    (void)sbw_jtag_shift_ir8(SBW_IR_DATA_TO_ADDR);
    (void)sbw_jtag_shift_dr16(data);

    sbw_jtag_tclk_set(false);
    (void)sbw_jtag_shift_ir8(SBW_IR_CNTRL_SIG_16BIT);
    (void)sbw_jtag_shift_dr16(0x0501);
    sbw_jtag_tclk_set(true);
    sbw_jtag_tclk_set(false);
    sbw_jtag_tclk_set(true);

    return sbw_jtag_in_full_emulation(NULL);
}

static bool sbw_jtag_disable_watchdog(void) {
    return sbw_jtag_write_mem16_internal(SBW_WDTCTL_ADDR_FR4XX, SBW_WDTCTL_HOLD);
}

static bool sbw_jtag_prepare_cpu(uint16_t *control_capture) {
    if (!sbw_jtag_sync_cpu()) {
        return false;
    }

    if (!sbw_jtag_execute_por(control_capture)) {
        return false;
    }

    return sbw_jtag_disable_watchdog();
}

static bool sbw_jtag_read_mem16_internal(uint32_t address, uint16_t *data) {
    if (!sbw_jtag_in_full_emulation(NULL)) {
        return false;
    }

    sbw_jtag_tclk_set(false);
    (void)sbw_jtag_shift_ir8(SBW_IR_CNTRL_SIG_16BIT);
    (void)sbw_jtag_shift_dr16(0x0501);
    (void)sbw_jtag_shift_ir8(SBW_IR_ADDR_16BIT);
    (void)sbw_jtag_shift_dr20(address & 0x000FFFFFu);
    (void)sbw_jtag_shift_ir8(SBW_IR_DATA_TO_ADDR);
    sbw_jtag_tclk_set(true);
    sbw_jtag_tclk_set(false);

    const uint16_t value = sbw_jtag_shift_dr16(0x0000);

    sbw_jtag_tclk_set(true);
    sbw_jtag_tclk_set(false);
    sbw_jtag_tclk_set(true);

    if (data) {
        *data = value;
    }

    return sbw_jtag_in_full_emulation(NULL);
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

bool sbw_jtag_sync_and_por(uint16_t *control_capture) {
    uint16_t last_capture = 0;

    for (uint32_t attempt = 0; attempt < SBW_JTAG_ATTEMPTS; ++attempt) {
        sbw_jtag_begin_session();
        const bool ok = sbw_jtag_sync_cpu() && sbw_jtag_execute_por(&last_capture);
        sbw_transport_release();

        if (ok) {
            if (control_capture) {
                *control_capture = last_capture;
            }
            return true;
        }
    }

    if (control_capture) {
        *control_capture = last_capture;
    }

    return false;
}

bool sbw_jtag_read_mem16(uint32_t address, uint16_t *data) {
    uint16_t last_data = 0;

    for (uint32_t attempt = 0; attempt < SBW_JTAG_ATTEMPTS; ++attempt) {
        sbw_jtag_begin_session();
        const bool ok = sbw_jtag_prepare_cpu(NULL) &&
            sbw_jtag_read_mem16_internal(address, &last_data);
        sbw_transport_release();

        if (ok) {
            if (data) {
                *data = last_data;
            }
            return true;
        }
    }

    if (data) {
        *data = last_data;
    }

    return false;
}

bool sbw_jtag_write_mem16(uint32_t address, uint16_t value, uint16_t *readback) {
    uint16_t last_readback = 0;

    for (uint32_t attempt = 0; attempt < SBW_JTAG_ATTEMPTS; ++attempt) {
        sbw_jtag_begin_session();
        const bool ok = sbw_jtag_prepare_cpu(NULL) &&
            sbw_jtag_write_mem16_internal(address, value) &&
            sbw_jtag_read_mem16_internal(address, &last_readback);
        sbw_transport_release();

        if (ok) {
            if (readback) {
                *readback = last_readback;
            }
            return true;
        }
    }

    if (readback) {
        *readback = last_readback;
    }

    return false;
}
