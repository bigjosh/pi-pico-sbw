#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "py/dynruntime.h"

enum {
    SBW_SYS_CLK_HZ = 150000000u,
    SBW_CYCLES_PER_MS = SBW_SYS_CLK_HZ / 1000u,
    SBW_JTAG_ATTEMPTS = 3,
    SBW_JTAG_TAP_RESET_BITS = 6,
    SBW_JTAG_ID_EXPECTED = 0x98,
    SBW_BYPASS_SMOKE_PATTERN = 0xA55A,
    SBW_BYPASS_SMOKE_EXPECTED = 0x52AD,
    SBW_JTAG_SYNC_RETRIES = 50,
    SBW_JTAG_SYNC_MASK = 0x0200,
    SBW_JTAG_FULL_EMULATION_MASK = 0x0301,
    SBW_IR_CNTRL_SIG_16BIT = 0x13,
    SBW_IR_CNTRL_SIG_CAPTURE = 0x14,
    SBW_IR_DATA_16BIT = 0x41,
    SBW_IR_DATA_CAPTURE = 0x42,
    SBW_IR_DATA_QUICK = 0x43,
    SBW_IR_ADDR_16BIT = 0x83,
    SBW_IR_ADDR_CAPTURE = 0x84,
    SBW_IR_DATA_TO_ADDR = 0x85,
    SBW_IR_BYPASS = 0xFF,
    SBW_SAFE_ACCESS_PC = 0x0004,
    SBW_SYSCFG0_ADDR_FR4XX = 0x0160,
    SBW_SYSCFG0_FRAM_PASSWORD = 0xA500,
    SBW_SYSCFG0_PFWP = 0x0001,
    SBW_SYSCFG0_DFWP = 0x0002,
    SBW_WDTCTL_ADDR_FR4XX = 0x01CC,
    SBW_WDTCTL_HOLD = 0x5A80,
    SBW_INFO_FRAM_START_FR4133 = 0x1800,
    SBW_INFO_FRAM_END_FR4133 = 0x19FF,
    SBW_MAIN_FRAM_START_FR4133 = 0xC400,
    SBW_MAIN_FRAM_END_FR4133 = 0xFFFF,
    SBW_NOP = 0x4303,
};

#define SBW_NS_TO_CYCLES(ns) ((uint32_t)((((uint64_t)SBW_SYS_CLK_HZ) * (uint64_t)(ns) + 999999999ull) / 1000000000ull))

enum {
    SBW_ACTIVE_SLOT_LOW_CYCLES = SBW_NS_TO_CYCLES(50),
    SBW_ACTIVE_SLOT_HIGH_CYCLES = 20,
    SBW_EXIT_HOLD_CYCLES = SBW_NS_TO_CYCLES(200000),
    SBW_ENTRY_RST_HIGH_TEST_RESET_CYCLES = 4u * SBW_CYCLES_PER_MS,
    SBW_ENTRY_RST_HIGH_TEST_ENABLE_CYCLES = 20u * SBW_CYCLES_PER_MS,
    SBW_ENTRY_RST_HIGH_SETUP_CYCLES = SBW_NS_TO_CYCLES(60000),
    SBW_ENTRY_RST_LOW_TEST_RESET_CYCLES = 1u * SBW_CYCLES_PER_MS,
    SBW_ENTRY_RST_LOW_HOLD_RESET_CYCLES = 50u * SBW_CYCLES_PER_MS,
    SBW_ENTRY_RST_LOW_TEST_ENABLE_CYCLES = 100u * SBW_CYCLES_PER_MS,
    SBW_ENTRY_RST_LOW_SETUP_CYCLES = SBW_NS_TO_CYCLES(40000),
    SBW_ENTRY_PULSE_LOW_CYCLES = SBW_NS_TO_CYCLES(1000),
    SBW_ENTRY_POST_ENABLE_CYCLES = 5u * SBW_CYCLES_PER_MS,
    SBW_JTAG_RETRY_SETTLE_CYCLES = 15u * SBW_CYCLES_PER_MS,
    SBW_TMSLDH_LOW_BEFORE_DRIVE_CYCLES = SBW_ACTIVE_SLOT_LOW_CYCLES / 2u,
};

enum {
    SIO_GPIO_IN      = 0x04 / 4,   /* SIO_BASE + 0x04 */
    SIO_GPIO_OUT_SET = 0x18 / 4,   /* SIO_BASE + 0x18 */
    SIO_GPIO_OUT_CLR = 0x20 / 4,   /* SIO_BASE + 0x20 */
    SIO_GPIO_OE_SET  = 0x38 / 4,   /* SIO_BASE + 0x38 */
    SIO_GPIO_OE_CLR  = 0x40 / 4,   /* SIO_BASE + 0x40 */
};

typedef struct {
    volatile uint32_t *sio;
    uint32_t clk;
    uint32_t data;
    bool tclk_high;
} sbw_ctx_t;

static inline void sbw_wait_cycles(uint32_t cycles) {
    if (cycles == 0) {
        return;
    }

    __asm__ volatile (
        "1:\n"
        "subs %0, #3\n"
        "bcs 1b\n"
        : "+r"(cycles)
        :
        : "cc", "memory");
}

static inline void sbw_irq_disable(void) {
    __asm__ volatile (
        "cpsid i\n"
        :
        :
        : "memory");
}

static inline void sbw_irq_enable(void) {
    __asm__ volatile (
        "cpsie i\n"
        :
        :
        : "memory");
}

/*
 * Transport invariants for this file:
 *
 * - ctx->tclk_high tracks the logical TCLK state latched in the last TDI slot,
 *   not the instantaneous SBWTCK pin level.
 * - During an active SBW/JTAG session, the master keeps SBWTDIO driven between
 *   logical bit cycles. sbw_io_bit() and sbw_tclk_set() assume the line is
 *   already owned on entry, release it only for the TDO slot, and reacquire it
 *   on the closing SBWTCK rising edge.
 * - sbw_ctx_init() and sbw_release() are explicit boundary states that leave
 *   SBWTDIO Hi-Z and SBWTCK low. sbw_entry_rst_high() / sbw_entry_rst_low()
 *   re-establish driven ownership before active bit traffic starts.
 * - Any helper that drives SBWTCK low must keep IRQs masked until SBWTCK is
 *   high again, or an overlong low phase can drop the active SBW session.
 */
static void sbw_ctx_init(sbw_ctx_t *ctx, mp_obj_t hw_in) {
    size_t len = 0;
    mp_obj_t *items = NULL;
    mp_obj_get_array(hw_in, &len, &items);
    if (len != 3) {
        mp_raise_ValueError("hw tuple must have 3 items");
    }

    ctx->sio = (volatile uint32_t *)(uintptr_t)(uint32_t)mp_obj_get_int_truncated(items[0]);
    ctx->clk = (uint32_t)mp_obj_get_int_truncated(items[1]);
    ctx->data = (uint32_t)mp_obj_get_int_truncated(items[2]);
    ctx->tclk_high = true;

    if (ctx->clk == 0 || ctx->data == 0) {
        mp_raise_ValueError("hw masks must be non-zero");
    }

    ctx->sio[SIO_GPIO_OE_SET] = ctx->clk;
    ctx->sio[SIO_GPIO_OE_CLR] = ctx->data;
    ctx->sio[SIO_GPIO_OUT_CLR] = ctx->clk;
}

static inline void sbw_clock_drive(sbw_ctx_t *ctx, bool high) {
    if (high) {
        ctx->sio[SIO_GPIO_OUT_SET] = ctx->clk;
    } else {
        ctx->sio[SIO_GPIO_OUT_CLR] = ctx->clk;
    }
}

static inline void sbw_data_drive(sbw_ctx_t *ctx, bool level) {
    if (level) {
        ctx->sio[SIO_GPIO_OUT_SET] = ctx->data;
    } else {
        ctx->sio[SIO_GPIO_OUT_CLR] = ctx->data;
    }
}

static inline void sbw_data_release_now(sbw_ctx_t *ctx) {
    ctx->sio[SIO_GPIO_OE_CLR] = ctx->data;
}

static inline bool sbw_data_read(const sbw_ctx_t *ctx) {
    return (ctx->sio[SIO_GPIO_IN] & ctx->data) != 0;
}

static inline void sbw_low_phase_begin(sbw_ctx_t *ctx) {
    sbw_irq_disable();
    sbw_clock_drive(ctx, false);
}

static inline void sbw_low_phase_end(sbw_ctx_t *ctx) {
    sbw_clock_drive(ctx, true);
    sbw_irq_enable();
}

static inline void sbw_pulse_low_cycles(sbw_ctx_t *ctx, uint32_t low_cycles) {
    sbw_low_phase_begin(ctx);
    sbw_wait_cycles(low_cycles);
    sbw_low_phase_end(ctx);
}

static void sbw_release(sbw_ctx_t *ctx) {
    ctx->tclk_high = true;
    sbw_data_release_now(ctx);
    sbw_clock_drive(ctx, false);
    sbw_wait_cycles(SBW_EXIT_HOLD_CYCLES);
}

static void sbw_entry_rst_high(sbw_ctx_t *ctx) {
    sbw_clock_drive(ctx, false);
    sbw_wait_cycles(SBW_ENTRY_RST_HIGH_TEST_RESET_CYCLES);

    ctx->sio[SIO_GPIO_OE_SET] = ctx->data;
    sbw_data_drive(ctx, true);
    sbw_clock_drive(ctx, true);
    sbw_wait_cycles(SBW_ENTRY_RST_HIGH_TEST_ENABLE_CYCLES);

    sbw_data_drive(ctx, true);
    sbw_wait_cycles(SBW_ENTRY_RST_HIGH_SETUP_CYCLES);
    sbw_pulse_low_cycles(ctx, SBW_ENTRY_PULSE_LOW_CYCLES);
    sbw_wait_cycles(SBW_ENTRY_RST_HIGH_SETUP_CYCLES);
    sbw_wait_cycles(SBW_ENTRY_POST_ENABLE_CYCLES);
}

static void sbw_entry_rst_low(sbw_ctx_t *ctx) {
    sbw_clock_drive(ctx, false);
    sbw_wait_cycles(SBW_ENTRY_RST_LOW_TEST_RESET_CYCLES);

    ctx->sio[SIO_GPIO_OE_SET] = ctx->data;
    sbw_data_drive(ctx, false);
    sbw_wait_cycles(SBW_ENTRY_RST_LOW_HOLD_RESET_CYCLES);

    sbw_clock_drive(ctx, true);
    sbw_wait_cycles(SBW_ENTRY_RST_LOW_TEST_ENABLE_CYCLES);

    sbw_data_drive(ctx, true);
    sbw_wait_cycles(SBW_ENTRY_RST_LOW_SETUP_CYCLES);
    sbw_pulse_low_cycles(ctx, SBW_ENTRY_PULSE_LOW_CYCLES);
    sbw_wait_cycles(SBW_ENTRY_RST_LOW_SETUP_CYCLES);
    sbw_wait_cycles(SBW_ENTRY_POST_ENABLE_CYCLES);
}

static void sbw_start_mode(sbw_ctx_t *ctx, bool rst_low) {
    sbw_release(ctx);
    if (rst_low) {
        sbw_entry_rst_low(ctx);
    } else {
        sbw_entry_rst_high(ctx);
    }
    ctx->tclk_high = true;
}

static inline bool sbw_io_bit(sbw_ctx_t *ctx, bool tms, bool tdi) {
    volatile uint32_t *const sio = ctx->sio;
    const uint32_t clk = ctx->clk;
    const uint32_t data = ctx->data;

    // One logical SBW bit is three clocked sub-slots on SBWTDIO: drive TMS, drive TDI, then
    // release the line so the target can return TDO on the third low pulse.
    // Active-session callers keep SBWTDIO driven between logical bits, so this sequence starts
    // with the master already owning the bus.
    // Slot 1: present TMS while we still own SBWTDIO.
    sio[tms ? SIO_GPIO_OUT_SET : SIO_GPIO_OUT_CLR] = data;
    sbw_wait_cycles(SBW_ACTIVE_SLOT_HIGH_CYCLES);
    sbw_irq_disable();
    sio[SIO_GPIO_OUT_CLR] = clk;
    sbw_wait_cycles(SBW_ACTIVE_SLOT_LOW_CYCLES);
    sio[SIO_GPIO_OUT_SET] = clk;
    sbw_irq_enable();

    // Slot 2: present TDI. In Run-Test/Idle this slot also carries TCLK state.
    sio[tdi ? SIO_GPIO_OUT_SET : SIO_GPIO_OUT_CLR] = data;
    sbw_wait_cycles(SBW_ACTIVE_SLOT_HIGH_CYCLES);
    sbw_irq_disable();
    sio[SIO_GPIO_OUT_CLR] = clk;
    sbw_wait_cycles(SBW_ACTIVE_SLOT_LOW_CYCLES);
    sio[SIO_GPIO_OUT_SET] = clk;
    sbw_irq_enable();

    // Slot 3: release SBWTDIO before the falling edge so the target can drive TDO.
    // Once SBWTCK returns high, the target should have released the line, so we can
    // immediately take ownership again instead of leaving the bus floating between bits.
    sio[SIO_GPIO_OE_CLR] = data;
    sbw_wait_cycles(SBW_ACTIVE_SLOT_HIGH_CYCLES);
    sbw_irq_disable();
    sio[SIO_GPIO_OUT_CLR] = clk;
    sbw_wait_cycles(SBW_ACTIVE_SLOT_LOW_CYCLES);
    const bool tdo = (sio[SIO_GPIO_IN] & data) != 0;
    sio[SIO_GPIO_OUT_SET] = clk;
    sio[SIO_GPIO_OE_SET] = data;
    sbw_irq_enable();
    return tdo;
}

static inline void sbw_tclk_set(sbw_ctx_t *ctx, bool high) {
    volatile uint32_t *const sio = ctx->sio;
    const uint32_t clk = ctx->clk;
    const uint32_t data = ctx->data;

    if (ctx->tclk_high) {
        sio[SIO_GPIO_OUT_CLR] = data;
        sbw_wait_cycles(SBW_ACTIVE_SLOT_HIGH_CYCLES);
        sbw_irq_disable();
        sio[SIO_GPIO_OUT_CLR] = clk;
        sbw_wait_cycles(SBW_TMSLDH_LOW_BEFORE_DRIVE_CYCLES);
        sio[SIO_GPIO_OUT_SET] = data;
        sbw_wait_cycles(SBW_ACTIVE_SLOT_LOW_CYCLES - SBW_TMSLDH_LOW_BEFORE_DRIVE_CYCLES);
        sio[SIO_GPIO_OUT_SET] = clk;
        sbw_irq_enable();
    } else {
        sio[SIO_GPIO_OUT_CLR] = data;
        sbw_wait_cycles(SBW_ACTIVE_SLOT_HIGH_CYCLES);
        sbw_irq_disable();
        sio[SIO_GPIO_OUT_CLR] = clk;
        sbw_wait_cycles(SBW_ACTIVE_SLOT_LOW_CYCLES);
        sio[SIO_GPIO_OUT_SET] = clk;
        sbw_irq_enable();
    }

    sio[high ? SIO_GPIO_OUT_SET : SIO_GPIO_OUT_CLR] = data;
    sbw_wait_cycles(SBW_ACTIVE_SLOT_HIGH_CYCLES);
    sbw_irq_disable();
    sio[SIO_GPIO_OUT_CLR] = clk;
    sbw_wait_cycles(SBW_ACTIVE_SLOT_LOW_CYCLES);
    sio[SIO_GPIO_OUT_SET] = clk;
    sbw_irq_enable();

    sio[SIO_GPIO_OE_CLR] = data;
    sbw_wait_cycles(SBW_ACTIVE_SLOT_HIGH_CYCLES);
    sbw_irq_disable();
    sio[SIO_GPIO_OUT_CLR] = clk;
    sbw_wait_cycles(SBW_ACTIVE_SLOT_LOW_CYCLES);
    sio[SIO_GPIO_OUT_SET] = clk;
    sio[SIO_GPIO_OE_SET] = data;
    sbw_irq_enable();
    ctx->tclk_high = high;
}

static void sbw_jtag_tap_reset(sbw_ctx_t *ctx) {
    for (uint32_t bit = 0; bit < SBW_JTAG_TAP_RESET_BITS; ++bit) {
        sbw_io_bit(ctx, true, true);
    }
    sbw_io_bit(ctx, false, true);
}

static void sbw_begin_session(sbw_ctx_t *ctx) {
    sbw_release(ctx);
    sbw_wait_cycles(SBW_JTAG_RETRY_SETTLE_CYCLES);
    sbw_start_mode(ctx, false);
    sbw_jtag_tap_reset(ctx);
}

static inline void sbw_go_to_shift_ir(sbw_ctx_t *ctx) {
    sbw_io_bit(ctx, true, ctx->tclk_high);
    sbw_io_bit(ctx, true, true);
    sbw_io_bit(ctx, false, true);
    sbw_io_bit(ctx, false, true);
}

static inline void sbw_go_to_shift_dr(sbw_ctx_t *ctx) {
    sbw_io_bit(ctx, true, ctx->tclk_high);
    sbw_io_bit(ctx, false, true);
    sbw_io_bit(ctx, false, true);
}

static inline void sbw_finish_shift(sbw_ctx_t *ctx) {
    sbw_io_bit(ctx, true, true);
    sbw_io_bit(ctx, false, ctx->tclk_high);
}

static void sbw_shift_ir8_no_capture(sbw_ctx_t *ctx, uint8_t instruction) {
    uint8_t shift = instruction;

    sbw_go_to_shift_ir(ctx);

    for (uint32_t bit = 0; bit < 7; ++bit) {
        sbw_io_bit(ctx, false, (shift & 0x1u) != 0);
        shift >>= 1;
    }

    sbw_io_bit(ctx, true, (shift & 0x1u) != 0);
    sbw_finish_shift(ctx);
}

static uint8_t sbw_shift_ir8_capture(sbw_ctx_t *ctx, uint8_t instruction) {
    uint8_t captured = 0;
    uint8_t shift = instruction;

    sbw_go_to_shift_ir(ctx);

    for (uint32_t bit = 0; bit < 7; ++bit) {
        captured = (uint8_t)((captured << 1) | (sbw_io_bit(ctx, false, (shift & 0x1u) != 0) ? 1u : 0u));
        shift >>= 1;
    }

    captured = (uint8_t)((captured << 1) | (sbw_io_bit(ctx, true, (shift & 0x1u) != 0) ? 1u : 0u));
    sbw_finish_shift(ctx);
    return captured;
}

static void sbw_shift_dr16_no_capture(sbw_ctx_t *ctx, uint16_t data) {
    uint16_t shift = data;

    sbw_go_to_shift_dr(ctx);

    for (uint32_t bit = 0; bit < 15; ++bit) {
        sbw_io_bit(ctx, false, (shift & 0x8000u) != 0);
        shift <<= 1;
    }

    sbw_io_bit(ctx, true, (shift & 0x8000u) != 0);
    sbw_finish_shift(ctx);
}

static uint16_t sbw_shift_dr16_capture(sbw_ctx_t *ctx, uint16_t data) {
    uint16_t captured = 0;
    uint16_t shift = data;

    sbw_go_to_shift_dr(ctx);

    for (uint32_t bit = 0; bit < 15; ++bit) {
        const bool tdi = (shift & 0x8000u) != 0;
        captured = (uint16_t)((captured << 1) | (sbw_io_bit(ctx, false, tdi) ? 1u : 0u));
        shift <<= 1;
    }

    captured = (uint16_t)((captured << 1) | (sbw_io_bit(ctx, true, (shift & 0x8000u) != 0) ? 1u : 0u));
    sbw_finish_shift(ctx);
    return captured;
}

static void sbw_shift_dr20_no_capture(sbw_ctx_t *ctx, uint32_t data) {
    uint32_t shift = data & 0x000FFFFFu;

    sbw_go_to_shift_dr(ctx);

    for (uint32_t bit = 0; bit < 19; ++bit) {
        sbw_io_bit(ctx, false, (shift & 0x00080000u) != 0);
        shift <<= 1;
    }

    sbw_io_bit(ctx, true, (shift & 0x00080000u) != 0);
    sbw_finish_shift(ctx);
}

static uint16_t sbw_read_control_signal(sbw_ctx_t *ctx) {
    sbw_shift_ir8_no_capture(ctx, SBW_IR_CNTRL_SIG_CAPTURE);
    return sbw_shift_dr16_capture(ctx, 0x0000);
}

static bool sbw_in_full_emulation(sbw_ctx_t *ctx, uint16_t *control_capture) {
    const uint16_t status = sbw_read_control_signal(ctx);
    if (control_capture) {
        *control_capture = status;
    }
    return (status & SBW_JTAG_FULL_EMULATION_MASK) == SBW_JTAG_FULL_EMULATION_MASK;
}

static bool sbw_sync_cpu(sbw_ctx_t *ctx) {
    sbw_shift_ir8_no_capture(ctx, SBW_IR_CNTRL_SIG_16BIT);
    sbw_shift_dr16_no_capture(ctx, 0x1501);

    if (sbw_shift_ir8_capture(ctx, SBW_IR_CNTRL_SIG_CAPTURE) != SBW_JTAG_ID_EXPECTED) {
        return false;
    }

    for (uint32_t attempt = 0; attempt < SBW_JTAG_SYNC_RETRIES; ++attempt) {
        if ((sbw_shift_dr16_capture(ctx, 0x0000) & SBW_JTAG_SYNC_MASK) != 0) {
            return true;
        }
    }

    return false;
}

static bool sbw_execute_por(sbw_ctx_t *ctx, uint16_t *control_capture) {
    sbw_tclk_set(ctx, false);
    sbw_tclk_set(ctx, true);

    sbw_shift_ir8_no_capture(ctx, SBW_IR_CNTRL_SIG_16BIT);
    sbw_shift_dr16_no_capture(ctx, 0x0C01);
    sbw_shift_dr16_no_capture(ctx, 0x0401);

    sbw_shift_ir8_no_capture(ctx, SBW_IR_DATA_16BIT);
    sbw_tclk_set(ctx, false);
    sbw_tclk_set(ctx, true);
    sbw_tclk_set(ctx, false);
    sbw_tclk_set(ctx, true);
    sbw_shift_dr16_no_capture(ctx, SBW_SAFE_ACCESS_PC);

    sbw_tclk_set(ctx, false);
    sbw_tclk_set(ctx, true);

    sbw_shift_ir8_no_capture(ctx, SBW_IR_DATA_CAPTURE);

    sbw_tclk_set(ctx, false);
    sbw_tclk_set(ctx, true);
    sbw_tclk_set(ctx, false);
    sbw_tclk_set(ctx, true);

    sbw_shift_ir8_no_capture(ctx, SBW_IR_CNTRL_SIG_16BIT);
    sbw_shift_dr16_no_capture(ctx, 0x0501);
    sbw_tclk_set(ctx, false);
    sbw_tclk_set(ctx, true);

    sbw_shift_ir8_no_capture(ctx, SBW_IR_CNTRL_SIG_CAPTURE);
    const uint16_t status = sbw_shift_dr16_capture(ctx, 0x0000);
    if (control_capture) {
        *control_capture = status;
    }
    return (status & SBW_JTAG_FULL_EMULATION_MASK) == SBW_JTAG_FULL_EMULATION_MASK;
}

static void sbw_write_mem16_sequence(sbw_ctx_t *ctx, uint32_t address, uint16_t data) {
    sbw_tclk_set(ctx, false);
    sbw_shift_ir8_no_capture(ctx, SBW_IR_CNTRL_SIG_16BIT);
    sbw_shift_dr16_no_capture(ctx, 0x0500);
    sbw_shift_ir8_no_capture(ctx, SBW_IR_ADDR_16BIT);
    sbw_shift_dr20_no_capture(ctx, address & 0x000FFFFFu);

    sbw_tclk_set(ctx, true);
    sbw_shift_ir8_no_capture(ctx, SBW_IR_DATA_TO_ADDR);
    sbw_shift_dr16_no_capture(ctx, data);

    sbw_tclk_set(ctx, false);
    sbw_shift_ir8_no_capture(ctx, SBW_IR_CNTRL_SIG_16BIT);
    sbw_shift_dr16_no_capture(ctx, 0x0501);
    sbw_tclk_set(ctx, true);
    sbw_tclk_set(ctx, false);
    sbw_tclk_set(ctx, true);
}

static bool sbw_read_mem16_internal(sbw_ctx_t *ctx, uint32_t address, uint16_t *data);

static bool sbw_write_mem16_internal(sbw_ctx_t *ctx, uint32_t address, uint16_t value) {
    if (!sbw_in_full_emulation(ctx, NULL)) {
        return false;
    }

    sbw_write_mem16_sequence(ctx, address, value);
    return sbw_in_full_emulation(ctx, NULL);
}

static bool sbw_set_pc_430xv2(sbw_ctx_t *ctx, uint32_t address) {
    const uint32_t pc = address & 0x000FFFFFu;
    const uint16_t mova_imm20_pc = (uint16_t)((((pc >> 16) & 0xFu) << 8) | 0x0080u);

    if (!sbw_in_full_emulation(ctx, NULL)) {
        return false;
    }

    sbw_tclk_set(ctx, false);
    sbw_shift_ir8_no_capture(ctx, SBW_IR_DATA_16BIT);
    sbw_tclk_set(ctx, true);
    sbw_shift_dr16_no_capture(ctx, mova_imm20_pc);

    sbw_tclk_set(ctx, false);
    sbw_shift_ir8_no_capture(ctx, SBW_IR_CNTRL_SIG_16BIT);
    sbw_shift_dr16_no_capture(ctx, 0x1400);

    sbw_shift_ir8_no_capture(ctx, SBW_IR_DATA_16BIT);
    sbw_tclk_set(ctx, false);
    sbw_tclk_set(ctx, true);
    sbw_shift_dr16_no_capture(ctx, (uint16_t)pc);

    sbw_tclk_set(ctx, false);
    sbw_tclk_set(ctx, true);
    sbw_shift_dr16_no_capture(ctx, SBW_NOP);

    sbw_tclk_set(ctx, false);
    sbw_shift_ir8_no_capture(ctx, SBW_IR_ADDR_CAPTURE);
    sbw_shift_dr20_no_capture(ctx, 0x00000);
    return true;
}

static uint16_t sbw_fram_protection_mask(uint32_t address) {
    const uint32_t masked = address & 0x000FFFFFu;

    if (masked >= SBW_INFO_FRAM_START_FR4133 && masked <= SBW_INFO_FRAM_END_FR4133) {
        return SBW_SYSCFG0_DFWP;
    }

    if (masked >= SBW_MAIN_FRAM_START_FR4133 && masked <= SBW_MAIN_FRAM_END_FR4133) {
        return SBW_SYSCFG0_PFWP;
    }

    return 0;
}

static bool sbw_block_protection_mask(uint32_t address, size_t word_count, uint16_t *mask_out) {
    const uint32_t start = address & 0x000FFFFFu;

    if ((start & 0x1u) != 0) {
        return false;
    }

    if (word_count == 0) {
        if (mask_out) {
            *mask_out = sbw_fram_protection_mask(start);
        }
        return true;
    }

    const uint32_t max_words = (0x00100000u - start) >> 1;
    if (word_count > max_words) {
        return false;
    }

    const uint16_t first = sbw_fram_protection_mask(start);
    const uint16_t last = sbw_fram_protection_mask(start + (((uint32_t)word_count - 1u) << 1));
    if (first != last) {
        return false;
    }

    if (mask_out) {
        *mask_out = first;
    }
    return true;
}

static bool sbw_write_syscfg0_low(sbw_ctx_t *ctx, uint16_t low_bits) {
    return sbw_write_mem16_internal(ctx, SBW_SYSCFG0_ADDR_FR4XX,
        SBW_SYSCFG0_FRAM_PASSWORD | (low_bits & 0x00FFu));
}

static bool sbw_unlock_fram_for_address(sbw_ctx_t *ctx, uint32_t address, uint16_t *saved_low_bits, bool *changed) {
    const uint16_t protection_mask = sbw_fram_protection_mask(address);
    uint16_t syscfg0 = 0;

    if (saved_low_bits) {
        *saved_low_bits = 0;
    }

    if (changed) {
        *changed = false;
    }

    if (protection_mask == 0) {
        return true;
    }

    if (!sbw_read_mem16_internal(ctx, SBW_SYSCFG0_ADDR_FR4XX, &syscfg0)) {
        return false;
    }

    const uint16_t saved = syscfg0 & 0x00FFu;
    if (saved_low_bits) {
        *saved_low_bits = saved;
    }

    if ((saved & protection_mask) == 0) {
        return true;
    }

    if (!sbw_write_syscfg0_low(ctx, saved & (uint16_t)~protection_mask)) {
        return false;
    }

    if (changed) {
        *changed = true;
    }

    return true;
}

static bool sbw_restore_fram_protection(sbw_ctx_t *ctx, uint16_t saved_low_bits, bool changed) {
    if (!changed) {
        return true;
    }
    return sbw_write_syscfg0_low(ctx, saved_low_bits);
}

static bool sbw_write_target_word(sbw_ctx_t *ctx, uint32_t address, uint16_t value) {
    uint16_t saved_fram_cfg = 0;
    bool fram_cfg_changed = false;

    const bool write_ok = sbw_unlock_fram_for_address(ctx, address, &saved_fram_cfg, &fram_cfg_changed) &&
        sbw_write_mem16_internal(ctx, address, value);
    const bool restore_ok = sbw_restore_fram_protection(ctx, saved_fram_cfg, fram_cfg_changed);
    return write_ok && restore_ok;
}

static bool sbw_disable_watchdog(sbw_ctx_t *ctx) {
    return sbw_write_mem16_internal(ctx, SBW_WDTCTL_ADDR_FR4XX, SBW_WDTCTL_HOLD);
}

static bool sbw_prepare_cpu(sbw_ctx_t *ctx, uint16_t *control_capture) {
    if (!sbw_sync_cpu(ctx)) {
        return false;
    }

    if (!sbw_execute_por(ctx, control_capture)) {
        return false;
    }

    return sbw_disable_watchdog(ctx);
}

static bool sbw_read_mem16_internal(sbw_ctx_t *ctx, uint32_t address, uint16_t *data) {
    if (!sbw_in_full_emulation(ctx, NULL)) {
        return false;
    }

    sbw_tclk_set(ctx, false);
    sbw_shift_ir8_no_capture(ctx, SBW_IR_CNTRL_SIG_16BIT);
    sbw_shift_dr16_no_capture(ctx, 0x0501);
    sbw_shift_ir8_no_capture(ctx, SBW_IR_ADDR_16BIT);
    sbw_shift_dr20_no_capture(ctx, address & 0x000FFFFFu);
    sbw_shift_ir8_no_capture(ctx, SBW_IR_DATA_TO_ADDR);
    sbw_tclk_set(ctx, true);
    sbw_tclk_set(ctx, false);

    const uint16_t value = sbw_shift_dr16_capture(ctx, 0x0000);

    sbw_tclk_set(ctx, true);
    sbw_tclk_set(ctx, false);
    sbw_tclk_set(ctx, true);

    if (data) {
        *data = value;
    }

    return sbw_in_full_emulation(ctx, NULL);
}

static bool sbw_begin_read_block16_quick_430xv2(sbw_ctx_t *ctx, uint32_t address) {
    // On FR4133, the Data Quick read path works across the target memory map.
    if (!sbw_set_pc_430xv2(ctx, address)) {
        return false;
    }

    sbw_tclk_set(ctx, true);
    sbw_shift_ir8_no_capture(ctx, SBW_IR_CNTRL_SIG_16BIT);
    sbw_shift_dr16_no_capture(ctx, 0x0501);
    sbw_shift_ir8_no_capture(ctx, SBW_IR_ADDR_CAPTURE);
    sbw_shift_ir8_no_capture(ctx, SBW_IR_DATA_QUICK);
    sbw_tclk_set(ctx, true);
    return true;
}

static inline uint16_t sbw_read_block16_quick_word(sbw_ctx_t *ctx) {
    sbw_tclk_set(ctx, false);
    const uint16_t value = sbw_shift_dr16_capture(ctx, 0x0000);
    sbw_tclk_set(ctx, true);
    return value;
}

static bool sbw_finish_read_block16_quick_430xv2(sbw_ctx_t *ctx) {
    (void)ctx;
    return true;
}

static bool sbw_read_block16_internal(sbw_ctx_t *ctx, uint32_t address, uint16_t *data, size_t word_count) {
    if (!sbw_in_full_emulation(ctx, NULL)) {
        return false;
    }

    if (word_count == 0) {
        return true;
    }

    if (!data || !sbw_begin_read_block16_quick_430xv2(ctx, address)) {
        return false;
    }

    for (size_t index = 0; index < word_count; ++index) {
        data[index] = sbw_read_block16_quick_word(ctx);
    }

    return sbw_finish_read_block16_quick_430xv2(ctx);
}

static bool sbw_begin_write_block16_quick_430xv2(sbw_ctx_t *ctx, uint32_t address) {
    if (!sbw_set_pc_430xv2(ctx, (address - 2u) & 0x000FFFFFu)) {
        return false;
    }

    sbw_tclk_set(ctx, true);
    sbw_shift_ir8_no_capture(ctx, SBW_IR_CNTRL_SIG_16BIT);
    sbw_shift_dr16_no_capture(ctx, 0x0500);
    sbw_shift_ir8_no_capture(ctx, SBW_IR_ADDR_CAPTURE);
    sbw_shift_ir8_no_capture(ctx, SBW_IR_DATA_QUICK);
    sbw_tclk_set(ctx, true);

    // Empirically, FR4133 quick writes need one priming data shift before the first real word lands.
    sbw_shift_dr16_no_capture(ctx, 0x1111);
    sbw_tclk_set(ctx, false);
    sbw_tclk_set(ctx, true);
    return true;
}

static inline void sbw_write_block16_quick_word(sbw_ctx_t *ctx, uint16_t data) {
    sbw_shift_dr16_no_capture(ctx, data);
    sbw_tclk_set(ctx, false);
    sbw_tclk_set(ctx, true);
}

static bool sbw_finish_write_block16_quick_430xv2(sbw_ctx_t *ctx) {
    sbw_shift_ir8_no_capture(ctx, SBW_IR_CNTRL_SIG_16BIT);
    sbw_shift_dr16_no_capture(ctx, 0x0501);
    sbw_tclk_set(ctx, true);
    sbw_tclk_set(ctx, false);
    sbw_tclk_set(ctx, true);
    return sbw_in_full_emulation(ctx, NULL);
}

static bool sbw_write_block16_internal(sbw_ctx_t *ctx, uint32_t address, const uint16_t *data, size_t word_count) {
    if (!sbw_in_full_emulation(ctx, NULL)) {
        return false;
    }

    if (word_count == 0) {
        return true;
    }

    // Public contract: a block write must stay within a single writable region
    // class so the access strategy remains uniform across the whole block.
    // On FR4133 that means one of:
    // - RAM/peripheral (standard per-word path)
    // - info FRAM (FRAM protection + quick block-write path)
    // - main FRAM (FRAM protection + quick block-write path)
    uint16_t block_mask = 0;
    uint16_t saved_fram_cfg = 0;
    bool fram_cfg_changed = false;

    if (!sbw_block_protection_mask(address, word_count, &block_mask)) {
        return false;
    }

    if (!sbw_unlock_fram_for_address(ctx, address, &saved_fram_cfg, &fram_cfg_changed)) {
        return false;
    }

    if (block_mask == 0) {
        for (size_t index = 0; index < word_count; ++index) {
            sbw_write_mem16_sequence(ctx, address + (uint32_t)(index * 2u), data[index]);
        }

        const bool write_ok = sbw_in_full_emulation(ctx, NULL);
        const bool restore_ok = sbw_restore_fram_protection(ctx, saved_fram_cfg, fram_cfg_changed);
        return write_ok && restore_ok;
    }

    if (!sbw_begin_write_block16_quick_430xv2(ctx, address)) {
        (void)sbw_restore_fram_protection(ctx, saved_fram_cfg, fram_cfg_changed);
        return false;
    }

    for (size_t index = 0; index < word_count; ++index) {
        sbw_write_block16_quick_word(ctx, data[index]);
    }

    const bool write_ok = sbw_finish_write_block16_quick_430xv2(ctx);
    const bool restore_ok = sbw_restore_fram_protection(ctx, saved_fram_cfg, fram_cfg_changed);
    return write_ok && restore_ok;
}

static mp_obj_t sbw_make_bool_u8(bool ok, uint8_t value) {
    mp_obj_t items[2] = {
        mp_obj_new_bool(ok),
        mp_obj_new_int_from_uint(value),
    };
    return mp_obj_new_tuple(2, items);
}

static mp_obj_t sbw_make_bool_u16(bool ok, uint16_t value) {
    mp_obj_t items[2] = {
        mp_obj_new_bool(ok),
        mp_obj_new_int_from_uint(value),
    };
    return mp_obj_new_tuple(2, items);
}

static mp_obj_t sbw_make_bool_bytes(bool ok, const uint8_t *data, size_t len) {
    mp_obj_t items[2] = {
        mp_obj_new_bool(ok),
        (ok && len != 0) ? mp_obj_new_bytes(data, len) : mp_const_empty_bytes,
    };
    return mp_obj_new_tuple(2, items);
}

static mp_obj_t sbw_native_read_id(mp_obj_t hw_in) {
    sbw_ctx_t ctx;
    uint8_t last_id = 0;
    bool ok = false;

    sbw_ctx_init(&ctx, hw_in);

    for (uint32_t attempt = 0; attempt < SBW_JTAG_ATTEMPTS; ++attempt) {
        sbw_begin_session(&ctx);
        last_id = sbw_shift_ir8_capture(&ctx, SBW_IR_CNTRL_SIG_CAPTURE);
        sbw_release(&ctx);
        if (last_id == SBW_JTAG_ID_EXPECTED) {
            ok = true;
            break;
        }
    }

    return sbw_make_bool_u8(ok, last_id);
}
static MP_DEFINE_CONST_FUN_OBJ_1(sbw_native_read_id_obj, sbw_native_read_id);

static mp_obj_t sbw_native_bypass_test(mp_obj_t hw_in) {
    sbw_ctx_t ctx;
    uint16_t captured = 0;
    bool ok = false;

    sbw_ctx_init(&ctx, hw_in);

    for (uint32_t attempt = 0; attempt < SBW_JTAG_ATTEMPTS; ++attempt) {
        sbw_begin_session(&ctx);
        sbw_shift_ir8_no_capture(&ctx, SBW_IR_BYPASS);
        captured = sbw_shift_dr16_capture(&ctx, SBW_BYPASS_SMOKE_PATTERN);
        sbw_release(&ctx);
        if (captured == SBW_BYPASS_SMOKE_EXPECTED) {
            ok = true;
            break;
        }
    }

    return sbw_make_bool_u16(ok, captured);
}
static MP_DEFINE_CONST_FUN_OBJ_1(sbw_native_bypass_test_obj, sbw_native_bypass_test);

static mp_obj_t sbw_native_sync_and_por(mp_obj_t hw_in) {
    sbw_ctx_t ctx;
    uint16_t capture = 0;
    bool ok = false;

    sbw_ctx_init(&ctx, hw_in);

    for (uint32_t attempt = 0; attempt < SBW_JTAG_ATTEMPTS; ++attempt) {
        sbw_begin_session(&ctx);
        ok = sbw_prepare_cpu(&ctx, &capture);
        sbw_release(&ctx);
        if (ok) {
            break;
        }
    }

    return sbw_make_bool_u16(ok, capture);
}
static MP_DEFINE_CONST_FUN_OBJ_1(sbw_native_sync_and_por_obj, sbw_native_sync_and_por);

static mp_obj_t sbw_native_read_mem16(mp_obj_t hw_in, mp_obj_t address_in) {
    sbw_ctx_t ctx;
    const uint32_t address = (uint32_t)mp_obj_get_int_truncated(address_in);
    uint16_t data = 0;
    bool ok = false;

    sbw_ctx_init(&ctx, hw_in);

    for (uint32_t attempt = 0; attempt < SBW_JTAG_ATTEMPTS; ++attempt) {
        sbw_begin_session(&ctx);
        ok = sbw_prepare_cpu(&ctx, NULL) && sbw_read_mem16_internal(&ctx, address, &data);
        sbw_release(&ctx);
        if (ok) {
            break;
        }
    }

    return sbw_make_bool_u16(ok, data);
}
static MP_DEFINE_CONST_FUN_OBJ_2(sbw_native_read_mem16_obj, sbw_native_read_mem16);

static mp_obj_t sbw_native_read_block_common(mp_obj_t hw_in, mp_obj_t address_in, mp_obj_t words_in) {
    sbw_ctx_t ctx;
    const uint32_t address = (uint32_t)mp_obj_get_int_truncated(address_in);
    const size_t word_count = (size_t)mp_obj_get_int_truncated(words_in);
    uint16_t *buffer = NULL;
    bool ok = false;

    sbw_ctx_init(&ctx, hw_in);

    if (word_count != 0) {
        buffer = (uint16_t *)m_malloc(word_count * sizeof(uint16_t));
    }

    for (uint32_t attempt = 0; attempt < SBW_JTAG_ATTEMPTS; ++attempt) {
        sbw_begin_session(&ctx);
        ok = sbw_prepare_cpu(&ctx, NULL) && sbw_read_block16_internal(&ctx, address, buffer, word_count);
        sbw_release(&ctx);
        if (ok) {
            break;
        }
    }

    mp_obj_t result = sbw_make_bool_bytes(ok, (const uint8_t *)buffer, word_count * sizeof(uint16_t));
    if (buffer) {
        m_free(buffer);
    }
    return result;
}

static mp_obj_t sbw_native_read_block16(mp_obj_t hw_in, mp_obj_t address_in, mp_obj_t words_in) {
    return sbw_native_read_block_common(hw_in, address_in, words_in);
}
static MP_DEFINE_CONST_FUN_OBJ_3(sbw_native_read_block16_obj, sbw_native_read_block16);

static mp_obj_t sbw_native_write_mem16(mp_obj_t hw_in, mp_obj_t address_in, mp_obj_t value_in) {
    sbw_ctx_t ctx;
    const uint32_t address = (uint32_t)mp_obj_get_int_truncated(address_in);
    const uint16_t value = (uint16_t)mp_obj_get_int_truncated(value_in);
    uint16_t readback = 0;
    bool ok = false;

    sbw_ctx_init(&ctx, hw_in);

    for (uint32_t attempt = 0; attempt < SBW_JTAG_ATTEMPTS; ++attempt) {
        sbw_begin_session(&ctx);
        ok = sbw_prepare_cpu(&ctx, NULL) &&
            sbw_write_target_word(&ctx, address, value) &&
            sbw_read_mem16_internal(&ctx, address, &readback) &&
            readback == value;
        sbw_release(&ctx);
        if (ok) {
            break;
        }
    }

    return sbw_make_bool_u16(ok, readback);
}
static MP_DEFINE_CONST_FUN_OBJ_3(sbw_native_write_mem16_obj, sbw_native_write_mem16);

static mp_obj_t sbw_native_write_block16(mp_obj_t hw_in, mp_obj_t address_in, mp_obj_t data_in) {
    sbw_ctx_t ctx;
    mp_buffer_info_t bufinfo;
    const uint32_t address = (uint32_t)mp_obj_get_int_truncated(address_in);

    sbw_ctx_init(&ctx, hw_in);
    mp_get_buffer_raise(data_in, &bufinfo, MP_BUFFER_READ);

    if ((bufinfo.len & 1u) != 0) {
        mp_raise_ValueError("block data length must be even");
    }

    const size_t word_count = bufinfo.len / 2u;
    const uint8_t *bytes = (const uint8_t *)bufinfo.buf;
    uint16_t *words = NULL;
    bool ok = false;

    if (word_count != 0) {
        words = (uint16_t *)m_malloc(word_count * sizeof(uint16_t));
        for (size_t index = 0; index < word_count; ++index) {
            words[index] = (uint16_t)bytes[index * 2u] | ((uint16_t)bytes[index * 2u + 1u] << 8);
        }
    }

    for (uint32_t attempt = 0; attempt < SBW_JTAG_ATTEMPTS; ++attempt) {
        sbw_begin_session(&ctx);
        ok = sbw_prepare_cpu(&ctx, NULL) && sbw_write_block16_internal(&ctx, address, words, word_count);
        sbw_release(&ctx);
        if (ok) {
            break;
        }
    }

    if (words) {
        m_free(words);
    }

    return mp_obj_new_bool(ok);
}
static MP_DEFINE_CONST_FUN_OBJ_3(sbw_native_write_block16_obj, sbw_native_write_block16);

mp_obj_t mpy_init(mp_obj_fun_bc_t *self, size_t n_args, size_t n_kw, mp_obj_t *args) {
    MP_DYNRUNTIME_INIT_ENTRY

    mp_store_global(MP_QSTR_read_id, MP_OBJ_FROM_PTR(&sbw_native_read_id_obj));
    mp_store_global(MP_QSTR_bypass_test, MP_OBJ_FROM_PTR(&sbw_native_bypass_test_obj));
    mp_store_global(MP_QSTR_sync_and_por, MP_OBJ_FROM_PTR(&sbw_native_sync_and_por_obj));
    mp_store_global(MP_QSTR_read_mem16, MP_OBJ_FROM_PTR(&sbw_native_read_mem16_obj));
    mp_store_global(MP_QSTR_write_mem16, MP_OBJ_FROM_PTR(&sbw_native_write_mem16_obj));
    mp_store_global(MP_QSTR_read_block16, MP_OBJ_FROM_PTR(&sbw_native_read_block16_obj));
    mp_store_global(MP_QSTR_write_block16, MP_OBJ_FROM_PTR(&sbw_native_write_block16_obj));
    mp_store_global(MP_QSTR_SYS_CLK_HZ, mp_obj_new_int_from_uint(SBW_SYS_CLK_HZ));

    MP_DYNRUNTIME_INIT_EXIT
}
