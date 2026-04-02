#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "py/dynruntime.h"

#define SIO_BASE ((volatile uint32_t *)(uintptr_t)0xD0000000u)

enum {
    SIO_GPIO_IN      = 0x04 / 4,   /* SIO_BASE + 0x04 */
    SIO_GPIO_OUT_SET = 0x18 / 4,   /* SIO_BASE + 0x18 */
    SIO_GPIO_OUT_CLR = 0x20 / 4,   /* SIO_BASE + 0x20 */
    SIO_GPIO_OE_SET  = 0x38 / 4,   /* SIO_BASE + 0x38 */
    SIO_GPIO_OE_CLR  = 0x40 / 4,   /* SIO_BASE + 0x40 */
};

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
    SBW_ACTIVE_SLOT_HIGH_CYCLES = 0,
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
    __asm__ volatile ("cpsid i\n" ::: "memory");
}

static inline void sbw_irq_enable(void) {
    __asm__ volatile ("cpsie i\n" ::: "memory");
}

static void sbw_parse_hw(mp_obj_t hw_in, uint32_t *clk, uint32_t *dio) {
    size_t len = 0;
    mp_obj_t *items = NULL;
    mp_obj_get_array(hw_in, &len, &items);
    if (len != 2) {
        mp_raise_ValueError("hw tuple must have 2 items");
    }
    *clk = (uint32_t)mp_obj_get_int_truncated(items[0]);
    *dio = (uint32_t)mp_obj_get_int_truncated(items[1]);
    if (*clk == 0 || *dio == 0) {
        mp_raise_ValueError("hw masks must be non-zero");
    }
}

static void sbw_hw_init(const uint32_t clk, const uint32_t dio) {
    SIO_BASE[SIO_GPIO_OE_SET] = clk;
    SIO_BASE[SIO_GPIO_OE_CLR] = dio;
    SIO_BASE[SIO_GPIO_OUT_CLR] = clk;
}

static inline void sbw_pulse_low_cycles(const uint32_t clk, uint32_t low_cycles) {
    sbw_irq_disable();
    SIO_BASE[SIO_GPIO_OUT_CLR] = clk;
    sbw_wait_cycles(low_cycles);
    SIO_BASE[SIO_GPIO_OUT_SET] = clk;
    sbw_irq_enable();
}

static void sbw_release(const uint32_t clk, const uint32_t dio) {
    SIO_BASE[SIO_GPIO_OE_CLR] = dio;
    SIO_BASE[SIO_GPIO_OUT_CLR] = clk;
    sbw_wait_cycles(SBW_EXIT_HOLD_CYCLES);
}

static void sbw_entry_rst_high(const uint32_t clk, const uint32_t dio) {
    SIO_BASE[SIO_GPIO_OUT_CLR] = clk;
    sbw_wait_cycles(SBW_ENTRY_RST_HIGH_TEST_RESET_CYCLES);

    SIO_BASE[SIO_GPIO_OE_SET] = dio;
    SIO_BASE[SIO_GPIO_OUT_SET] = dio;
    SIO_BASE[SIO_GPIO_OUT_SET] = clk;
    sbw_wait_cycles(SBW_ENTRY_RST_HIGH_TEST_ENABLE_CYCLES);

    SIO_BASE[SIO_GPIO_OUT_SET] = dio;
    sbw_wait_cycles(SBW_ENTRY_RST_HIGH_SETUP_CYCLES);
    sbw_pulse_low_cycles(clk, SBW_ENTRY_PULSE_LOW_CYCLES);
    sbw_wait_cycles(SBW_ENTRY_RST_HIGH_SETUP_CYCLES);
    sbw_wait_cycles(SBW_ENTRY_POST_ENABLE_CYCLES);
}

static void sbw_entry_rst_low(const uint32_t clk, const uint32_t dio) {
    SIO_BASE[SIO_GPIO_OUT_CLR] = clk;
    sbw_wait_cycles(SBW_ENTRY_RST_LOW_TEST_RESET_CYCLES);

    SIO_BASE[SIO_GPIO_OE_SET] = dio;
    SIO_BASE[SIO_GPIO_OUT_CLR] = dio;
    sbw_wait_cycles(SBW_ENTRY_RST_LOW_HOLD_RESET_CYCLES);

    SIO_BASE[SIO_GPIO_OUT_SET] = clk;
    sbw_wait_cycles(SBW_ENTRY_RST_LOW_TEST_ENABLE_CYCLES);

    SIO_BASE[SIO_GPIO_OUT_SET] = dio;
    sbw_wait_cycles(SBW_ENTRY_RST_LOW_SETUP_CYCLES);
    sbw_pulse_low_cycles(clk, SBW_ENTRY_PULSE_LOW_CYCLES);
    sbw_wait_cycles(SBW_ENTRY_RST_LOW_SETUP_CYCLES);
    sbw_wait_cycles(SBW_ENTRY_POST_ENABLE_CYCLES);
}

static void sbw_start_mode(const uint32_t clk, const uint32_t dio, bool rst_low) {
    sbw_release(clk, dio);
    if (rst_low) {
        sbw_entry_rst_low(clk, dio);
    } else {
        sbw_entry_rst_high(clk, dio);
    }
}

static __attribute__((always_inline)) inline bool sbw_io_bit(const uint32_t clk, const uint32_t dio, bool tms, bool tdi) {
    // Slot 1: TMS
    SIO_BASE[tms ? SIO_GPIO_OUT_SET : SIO_GPIO_OUT_CLR] = dio;
    sbw_wait_cycles(SBW_ACTIVE_SLOT_HIGH_CYCLES);
    sbw_irq_disable();
    SIO_BASE[SIO_GPIO_OUT_CLR] = clk;
    sbw_wait_cycles(SBW_ACTIVE_SLOT_LOW_CYCLES);
    SIO_BASE[SIO_GPIO_OUT_SET] = clk;
    sbw_irq_enable();

    // Slot 2: TDI (carries TCLK in Run-Test/Idle)
    SIO_BASE[tdi ? SIO_GPIO_OUT_SET : SIO_GPIO_OUT_CLR] = dio;
    sbw_wait_cycles(SBW_ACTIVE_SLOT_HIGH_CYCLES);
    sbw_irq_disable();
    SIO_BASE[SIO_GPIO_OUT_CLR] = clk;
    sbw_wait_cycles(SBW_ACTIVE_SLOT_LOW_CYCLES);
    SIO_BASE[SIO_GPIO_OUT_SET] = clk;
    sbw_irq_enable();

    // Slot 3: TDO — release bus, clock, read, reacquire
    SIO_BASE[SIO_GPIO_OE_CLR] = dio;
    sbw_wait_cycles(SBW_ACTIVE_SLOT_HIGH_CYCLES);
    sbw_irq_disable();
    SIO_BASE[SIO_GPIO_OUT_CLR] = clk;
    sbw_wait_cycles(SBW_ACTIVE_SLOT_LOW_CYCLES);
    const bool tdo = (SIO_BASE[SIO_GPIO_IN] & dio) != 0;
    SIO_BASE[SIO_GPIO_OUT_SET] = clk;
    sbw_irq_enable();
    // There should probably be a wait here for the target to release the data pin, but in practice it seems to be fast enough that we can just set it back to output immediately.
    SIO_BASE[SIO_GPIO_OE_SET] = dio;
    return tdo;
}

// ClrTCLK: HIGH -> LOW. TMSLDH timing per SLAU320AJ 2.2.3.2.3.
// This is tricky - the dtaa must be high coming into TDI slot and then go low in the middle of the slot
// the set TCLK low. It is the transition that does the setting. 
// This always sets TMS low since we will always be in run-idle when we do this (that the only time you can change TCLK). 
static inline void sbw_clr_tclk(const uint32_t clk, const uint32_t dio) {
    // TMS slot: TMSLDH — drive low for TMS=0, back high during CLK low
    SIO_BASE[SIO_GPIO_OUT_CLR] = dio;
    sbw_wait_cycles(SBW_ACTIVE_SLOT_HIGH_CYCLES);
    sbw_irq_disable();
    SIO_BASE[SIO_GPIO_OUT_CLR] = clk;       // Target samples TMS=0 here
    sbw_wait_cycles(SBW_TMSLDH_LOW_BEFORE_DRIVE_CYCLES);
    SIO_BASE[SIO_GPIO_OUT_SET] = dio;       // Drive TMS back high while CLK still low so it will be high coming into TDI slot
    sbw_wait_cycles(SBW_ACTIVE_SLOT_LOW_CYCLES - SBW_TMSLDH_LOW_BEFORE_DRIVE_CYCLES);
    SIO_BASE[SIO_GPIO_OUT_SET] = clk;
    sbw_irq_enable();

    // TDI slot: data is already high 
    sbw_wait_cycles(SBW_ACTIVE_SLOT_HIGH_CYCLES);
    sbw_irq_disable();
    SIO_BASE[SIO_GPIO_OUT_CLR] = clk;
    sbw_wait_cycles(SBW_ACTIVE_SLOT_LOW_CYCLES);
    SIO_BASE[SIO_GPIO_OUT_CLR] = dio;   // Here data goes high->low, which sets MCLK low. 
    sbw_wait_cycles(SBW_ACTIVE_SLOT_LOW_CYCLES);    // Here we are ultra conservative since the docs arent really clear. 
    SIO_BASE[SIO_GPIO_OUT_SET] = clk;
    sbw_irq_enable();

    // TDO slot
    SIO_BASE[SIO_GPIO_OE_CLR] = dio;        // data pin to input
    sbw_wait_cycles(SBW_ACTIVE_SLOT_HIGH_CYCLES);
    sbw_irq_disable();
    SIO_BASE[SIO_GPIO_OUT_CLR] = clk;
    sbw_wait_cycles(SBW_ACTIVE_SLOT_LOW_CYCLES);
    SIO_BASE[SIO_GPIO_OUT_SET] = clk;
    // There should probably be a wait here for the target to release the data pin, but in practice it seems to be fast enough that we can just set it back to output immediately.
    SIO_BASE[SIO_GPIO_OE_SET] = dio;
    sbw_irq_enable();
}

// SetTCLK: LOW -> HIGH. See above for details (this is inverse of ClrTCLK) but easier becuase data will already be low coming into TDI
static inline void sbw_set_tclk(const uint32_t clk, const uint32_t dio) {
    // TMS slot: TMSLDH — drive low for TMS=0, back high during CLK low
    SIO_BASE[SIO_GPIO_OUT_CLR] = dio;
    sbw_wait_cycles(SBW_ACTIVE_SLOT_HIGH_CYCLES);
    sbw_irq_disable();
    SIO_BASE[SIO_GPIO_OUT_CLR] = clk;       // Target samples TMS=0 here
    sbw_wait_cycles(SBW_TMSLDH_LOW_BEFORE_DRIVE_CYCLES);
    SIO_BASE[SIO_GPIO_OUT_SET] = clk;
    sbw_irq_enable();

    // TDI slot: data is already LOW
    sbw_wait_cycles(SBW_ACTIVE_SLOT_HIGH_CYCLES);
    sbw_irq_disable();
    SIO_BASE[SIO_GPIO_OUT_CLR] = clk;
    sbw_wait_cycles(SBW_ACTIVE_SLOT_LOW_CYCLES);
    SIO_BASE[SIO_GPIO_OUT_SET] = dio;   // Here data goes low->high, which sets MCLK high. 
    sbw_wait_cycles(SBW_ACTIVE_SLOT_LOW_CYCLES);    // Here we are ultra conservative since the docs arent really clear. 
    SIO_BASE[SIO_GPIO_OUT_SET] = clk;
    sbw_irq_enable();

    // TDO slot
    SIO_BASE[SIO_GPIO_OE_CLR] = dio;        // data pin to input
    sbw_wait_cycles(SBW_ACTIVE_SLOT_HIGH_CYCLES);
    sbw_irq_disable();
    SIO_BASE[SIO_GPIO_OUT_CLR] = clk;
    sbw_wait_cycles(SBW_ACTIVE_SLOT_LOW_CYCLES);
    SIO_BASE[SIO_GPIO_OUT_SET] = clk;
    // There should probably be a wait here for the target to release the data pin, but in practice it seems to be fast enough that we can just set it back to output immediately.
    SIO_BASE[SIO_GPIO_OE_SET] = dio;
    sbw_irq_enable();
}

static void sbw_jtag_tap_reset(const uint32_t clk, const uint32_t dio) {
    for (uint32_t bit = 0; bit < SBW_JTAG_TAP_RESET_BITS; ++bit) {
        sbw_io_bit(clk, dio, true, true);
    }
    sbw_io_bit(clk, dio, false, true);
}

static void sbw_begin_session(const uint32_t clk, const uint32_t dio) {
    sbw_release(clk, dio);
    sbw_wait_cycles(SBW_JTAG_RETRY_SETTLE_CYCLES);
    sbw_start_mode(clk, dio, false);
    sbw_jtag_tap_reset(clk, dio);
}

static inline void sbw_go_to_shift_ir(const uint32_t clk, const uint32_t dio, bool tclk) {
    sbw_io_bit(clk, dio, true, tclk);
    sbw_io_bit(clk, dio, true, true);
    sbw_io_bit(clk, dio, false, true);
    sbw_io_bit(clk, dio, false, true);
}

static inline void sbw_go_to_shift_dr(const uint32_t clk, const uint32_t dio, bool tclk) {
    sbw_io_bit(clk, dio, true, tclk);
    sbw_io_bit(clk, dio, false, true);
    sbw_io_bit(clk, dio, false, true);
}

static inline void sbw_finish_shift(const uint32_t clk, const uint32_t dio, bool tclk) {
    sbw_io_bit(clk, dio, true, true);
    sbw_io_bit(clk, dio, false, tclk);
}

static void sbw_shift_ir8_no_capture(const uint32_t clk, const uint32_t dio, uint8_t instruction, bool tclk) {
    uint8_t shift = instruction;
    sbw_go_to_shift_ir(clk, dio, tclk);
    for (uint32_t bit = 0; bit < 7; ++bit) {
        sbw_io_bit(clk, dio, false, (shift & 0x1u) != 0);
        shift >>= 1;
    }
    sbw_io_bit(clk, dio, true, (shift & 0x1u) != 0);
    sbw_finish_shift(clk, dio, tclk);
}

static uint8_t sbw_shift_ir8_capture(const uint32_t clk, const uint32_t dio, uint8_t instruction, bool tclk) {
    uint8_t captured = 0;
    uint8_t shift = instruction;
    sbw_go_to_shift_ir(clk, dio, tclk);
    for (uint32_t bit = 0; bit < 7; ++bit) {
        captured = (uint8_t)((captured << 1) | (sbw_io_bit(clk, dio, false, (shift & 0x1u) != 0) ? 1u : 0u));
        shift >>= 1;
    }
    captured = (uint8_t)((captured << 1) | (sbw_io_bit(clk, dio, true, (shift & 0x1u) != 0) ? 1u : 0u));
    sbw_finish_shift(clk, dio, tclk);
    return captured;
}

static void sbw_shift_dr16_no_capture(const uint32_t clk, const uint32_t dio, uint16_t data, bool tclk) {
    uint16_t shift = data;
    sbw_go_to_shift_dr(clk, dio, tclk);
    for (uint32_t bit = 0; bit < 15; ++bit) {
        sbw_io_bit(clk, dio, false, (shift & 0x8000u) != 0);
        shift <<= 1;
    }
    sbw_io_bit(clk, dio, true, (shift & 0x8000u) != 0);
    sbw_finish_shift(clk, dio, tclk);
}

static uint16_t sbw_shift_dr16_capture(const uint32_t clk, const uint32_t dio, uint16_t data, bool tclk) {
    uint16_t captured = 0;
    uint16_t shift = data;
    sbw_go_to_shift_dr(clk, dio, tclk);
    for (uint32_t bit = 0; bit < 15; ++bit) {
        const bool tdi = (shift & 0x8000u) != 0;
        captured = (uint16_t)((captured << 1) | (sbw_io_bit(clk, dio, false, tdi) ? 1u : 0u));
        shift <<= 1;
    }
    captured = (uint16_t)((captured << 1) | (sbw_io_bit(clk, dio, true, (shift & 0x8000u) != 0) ? 1u : 0u));
    sbw_finish_shift(clk, dio, tclk);
    return captured;
}

static void sbw_shift_dr20_no_capture(const uint32_t clk, const uint32_t dio, uint32_t data, bool tclk) {
    uint32_t shift = data & 0x000FFFFFu;
    sbw_go_to_shift_dr(clk, dio, tclk);
    for (uint32_t bit = 0; bit < 19; ++bit) {
        sbw_io_bit(clk, dio, false, (shift & 0x00080000u) != 0);
        shift <<= 1;
    }
    sbw_io_bit(clk, dio, true, (shift & 0x00080000u) != 0);
    sbw_finish_shift(clk, dio, tclk);
}

static uint16_t sbw_read_control_signal(const uint32_t clk, const uint32_t dio) {
    sbw_shift_ir8_no_capture(clk, dio, SBW_IR_CNTRL_SIG_CAPTURE, true);
    return sbw_shift_dr16_capture(clk, dio, 0x0000, true);
}

static bool sbw_in_full_emulation(const uint32_t clk, const uint32_t dio, uint16_t *control_capture) {
    const uint16_t status = sbw_read_control_signal(clk, dio);
    if (control_capture) {
        *control_capture = status;
    }
    return (status & SBW_JTAG_FULL_EMULATION_MASK) == SBW_JTAG_FULL_EMULATION_MASK;
}

static bool sbw_sync_cpu(const uint32_t clk, const uint32_t dio) {
    sbw_shift_ir8_no_capture(clk, dio, SBW_IR_CNTRL_SIG_16BIT, true);
    sbw_shift_dr16_no_capture(clk, dio, 0x1501, true);

    if (sbw_shift_ir8_capture(clk, dio, SBW_IR_CNTRL_SIG_CAPTURE, true) != SBW_JTAG_ID_EXPECTED) {
        return false;
    }

    for (uint32_t attempt = 0; attempt < SBW_JTAG_SYNC_RETRIES; ++attempt) {
        if ((sbw_shift_dr16_capture(clk, dio, 0x0000, true) & SBW_JTAG_SYNC_MASK) != 0) {
            return true;
        }
    }

    return false;
}

static bool sbw_execute_por(const uint32_t clk, const uint32_t dio, uint16_t *control_capture) {
    sbw_clr_tclk(clk, dio);
    sbw_set_tclk(clk, dio);

    sbw_shift_ir8_no_capture(clk, dio, SBW_IR_CNTRL_SIG_16BIT, true);
    sbw_shift_dr16_no_capture(clk, dio, 0x0C01, true);
    sbw_shift_dr16_no_capture(clk, dio, 0x0401, true);

    sbw_shift_ir8_no_capture(clk, dio, SBW_IR_DATA_16BIT, true);
    sbw_clr_tclk(clk, dio);
    sbw_set_tclk(clk, dio);
    sbw_clr_tclk(clk, dio);
    sbw_set_tclk(clk, dio);
    sbw_shift_dr16_no_capture(clk, dio, SBW_SAFE_ACCESS_PC, true);

    sbw_clr_tclk(clk, dio);
    sbw_set_tclk(clk, dio);

    sbw_shift_ir8_no_capture(clk, dio, SBW_IR_DATA_CAPTURE, true);

    sbw_clr_tclk(clk, dio);
    sbw_set_tclk(clk, dio);
    sbw_clr_tclk(clk, dio);
    sbw_set_tclk(clk, dio);

    sbw_shift_ir8_no_capture(clk, dio, SBW_IR_CNTRL_SIG_16BIT, true);
    sbw_shift_dr16_no_capture(clk, dio, 0x0501, true);
    sbw_clr_tclk(clk, dio);
    sbw_set_tclk(clk, dio);

    sbw_shift_ir8_no_capture(clk, dio, SBW_IR_CNTRL_SIG_CAPTURE, true);
    const uint16_t status = sbw_shift_dr16_capture(clk, dio, 0x0000, true);
    if (control_capture) {
        *control_capture = status;
    }
    return (status & SBW_JTAG_FULL_EMULATION_MASK) == SBW_JTAG_FULL_EMULATION_MASK;
}

static void sbw_write_mem16_sequence(const uint32_t clk, const uint32_t dio, uint32_t address, uint16_t data) {
    sbw_clr_tclk(clk, dio);
    sbw_shift_ir8_no_capture(clk, dio, SBW_IR_CNTRL_SIG_16BIT, false);
    sbw_shift_dr16_no_capture(clk, dio, 0x0500, false);
    sbw_shift_ir8_no_capture(clk, dio, SBW_IR_ADDR_16BIT, false);
    sbw_shift_dr20_no_capture(clk, dio, address & 0x000FFFFFu, false);

    sbw_set_tclk(clk, dio);
    sbw_shift_ir8_no_capture(clk, dio, SBW_IR_DATA_TO_ADDR, true);
    sbw_shift_dr16_no_capture(clk, dio, data, true);

    sbw_clr_tclk(clk, dio);
    sbw_shift_ir8_no_capture(clk, dio, SBW_IR_CNTRL_SIG_16BIT, false);
    sbw_shift_dr16_no_capture(clk, dio, 0x0501, false);
    sbw_set_tclk(clk, dio);
    sbw_clr_tclk(clk, dio);
    sbw_set_tclk(clk, dio);
}

static bool sbw_read_mem16_internal(const uint32_t clk, const uint32_t dio, uint32_t address, uint16_t *data);

static bool sbw_write_mem16_internal(const uint32_t clk, const uint32_t dio, uint32_t address, uint16_t value) {
    if (!sbw_in_full_emulation(clk, dio, NULL)) {
        return false;
    }
    sbw_write_mem16_sequence(clk, dio, address, value);
    return sbw_in_full_emulation(clk, dio, NULL);
}

static bool sbw_set_pc_430xv2(const uint32_t clk, const uint32_t dio, uint32_t address) {
    const uint32_t pc = address & 0x000FFFFFu;
    const uint16_t mova_imm20_pc = (uint16_t)((((pc >> 16) & 0xFu) << 8) | 0x0080u);

    if (!sbw_in_full_emulation(clk, dio, NULL)) {
        return false;
    }

    sbw_clr_tclk(clk, dio);
    sbw_shift_ir8_no_capture(clk, dio, SBW_IR_DATA_16BIT, false);
    sbw_set_tclk(clk, dio);
    sbw_shift_dr16_no_capture(clk, dio, mova_imm20_pc, true);

    sbw_clr_tclk(clk, dio);
    sbw_shift_ir8_no_capture(clk, dio, SBW_IR_CNTRL_SIG_16BIT, false);
    sbw_shift_dr16_no_capture(clk, dio, 0x1400, false);

    sbw_shift_ir8_no_capture(clk, dio, SBW_IR_DATA_16BIT, false);
    sbw_clr_tclk(clk, dio);
    sbw_set_tclk(clk, dio);
    sbw_shift_dr16_no_capture(clk, dio, (uint16_t)pc, true);

    sbw_clr_tclk(clk, dio);
    sbw_set_tclk(clk, dio);
    sbw_shift_dr16_no_capture(clk, dio, SBW_NOP, true);

    sbw_clr_tclk(clk, dio);
    sbw_shift_ir8_no_capture(clk, dio, SBW_IR_ADDR_CAPTURE, false);
    sbw_shift_dr20_no_capture(clk, dio, 0x00000, false);
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

static bool sbw_write_syscfg0_low(const uint32_t clk, const uint32_t dio, uint16_t low_bits) {
    return sbw_write_mem16_internal(clk, dio, SBW_SYSCFG0_ADDR_FR4XX,
        SBW_SYSCFG0_FRAM_PASSWORD | (low_bits & 0x00FFu));
}

static bool sbw_unlock_fram_for_address(const uint32_t clk, const uint32_t dio, uint32_t address,
    uint16_t *saved_low_bits, bool *changed)
{
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

    if (!sbw_read_mem16_internal(clk, dio, SBW_SYSCFG0_ADDR_FR4XX, &syscfg0)) {
        return false;
    }

    const uint16_t saved = syscfg0 & 0x00FFu;
    if (saved_low_bits) {
        *saved_low_bits = saved;
    }
    if ((saved & protection_mask) == 0) {
        return true;
    }

    if (!sbw_write_syscfg0_low(clk, dio, saved & (uint16_t)~protection_mask)) {
        return false;
    }
    if (changed) {
        *changed = true;
    }
    return true;
}

static bool sbw_restore_fram_protection(const uint32_t clk, const uint32_t dio, uint16_t saved_low_bits, bool changed) {
    if (!changed) {
        return true;
    }
    return sbw_write_syscfg0_low(clk, dio, saved_low_bits);
}

static bool sbw_write_target_word(const uint32_t clk, const uint32_t dio, uint32_t address, uint16_t value) {
    uint16_t saved_fram_cfg = 0;
    bool fram_cfg_changed = false;
    const bool write_ok = sbw_unlock_fram_for_address(clk, dio, address, &saved_fram_cfg, &fram_cfg_changed) &&
        sbw_write_mem16_internal(clk, dio, address, value);
    const bool restore_ok = sbw_restore_fram_protection(clk, dio, saved_fram_cfg, fram_cfg_changed);
    return write_ok && restore_ok;
}

static bool sbw_disable_watchdog(const uint32_t clk, const uint32_t dio) {
    return sbw_write_mem16_internal(clk, dio, SBW_WDTCTL_ADDR_FR4XX, SBW_WDTCTL_HOLD);
}

static bool sbw_prepare_cpu(const uint32_t clk, const uint32_t dio, uint16_t *control_capture) {
    if (!sbw_sync_cpu(clk, dio)) {
        return false;
    }
    if (!sbw_execute_por(clk, dio, control_capture)) {
        return false;
    }
    return sbw_disable_watchdog(clk, dio);
}

static bool sbw_read_mem16_internal(const uint32_t clk, const uint32_t dio, uint32_t address, uint16_t *data) {
    if (!sbw_in_full_emulation(clk, dio, NULL)) {
        return false;
    }

    sbw_clr_tclk(clk, dio);
    sbw_shift_ir8_no_capture(clk, dio, SBW_IR_CNTRL_SIG_16BIT, false);
    sbw_shift_dr16_no_capture(clk, dio, 0x0501, false);
    sbw_shift_ir8_no_capture(clk, dio, SBW_IR_ADDR_16BIT, false);
    sbw_shift_dr20_no_capture(clk, dio, address & 0x000FFFFFu, false);
    sbw_shift_ir8_no_capture(clk, dio, SBW_IR_DATA_TO_ADDR, false);
    sbw_set_tclk(clk, dio);
    sbw_clr_tclk(clk, dio);

    const uint16_t value = sbw_shift_dr16_capture(clk, dio, 0x0000, false);

    sbw_set_tclk(clk, dio);
    sbw_clr_tclk(clk, dio);
    sbw_set_tclk(clk, dio);

    if (data) {
        *data = value;
    }
    return sbw_in_full_emulation(clk, dio, NULL);
}

static bool sbw_begin_read_block16_quick(const uint32_t clk, const uint32_t dio, uint32_t address) {
    if (!sbw_set_pc_430xv2(clk, dio, address)) {
        return false;
    }

    sbw_set_tclk(clk, dio);
    sbw_shift_ir8_no_capture(clk, dio, SBW_IR_CNTRL_SIG_16BIT, true);
    sbw_shift_dr16_no_capture(clk, dio, 0x0501, true);
    sbw_shift_ir8_no_capture(clk, dio, SBW_IR_ADDR_CAPTURE, true);
    sbw_shift_ir8_no_capture(clk, dio, SBW_IR_DATA_QUICK, true);
    return true;
}

static inline uint16_t sbw_read_block16_quick_word(const uint32_t clk, const uint32_t dio) {
    sbw_clr_tclk(clk, dio);
    const uint16_t value = sbw_shift_dr16_capture(clk, dio, 0x0000, false);
    sbw_set_tclk(clk, dio);
    return value;
}

static bool sbw_read_block16_internal(const uint32_t clk, const uint32_t dio,
    uint32_t address, uint16_t *data, size_t word_count)
{
    if (!sbw_in_full_emulation(clk, dio, NULL)) {
        return false;
    }
    if (word_count == 0) {
        return true;
    }
    if (!data || !sbw_begin_read_block16_quick(clk, dio, address)) {
        return false;
    }
    for (size_t index = 0; index < word_count; ++index) {
        data[index] = sbw_read_block16_quick_word(clk, dio);
    }
    return true;
}

/*
 * Quick block write for 430Xv2 using IR_DATA_QUICK.
 *
 * TI does not define a quick write sequence for 430Xv2 (SLAU320AJ 2.3.3.3.2
 * only covers 1xx/2xx/4xx). This implementation follows the DATA_QUICK bus
 * cycle model derived from SLAU320AJ 2.3.3.3 and verified on FR4133:
 *
 *   "The MDB should be set when TCLK is low. On the next rising TCLK edge,
 *    the value on the MDB is written into the location addressed by the PC."
 *
 *   "The PC is incremented by two with each falling edge of TCLK."
 *
 * Per-word write cycle:
 *
 *   DR_SHIFT16(data)   Load MDB via DATA_QUICK while TCLK=HIGH.
 *   ClrTCLK            Falling edge — PC auto-increments by 2.
 *   SetTCLK            Rising edge — commits MDB to memory[PC].
 *
 * The write commits on SetTCLK (rising edge) using the PC value AFTER the
 * preceding ClrTCLK increment. This means the first real write targets
 * PC_initial + 2. To compensate, SetPC targets address - 2, and the setup
 * includes one ClrTCLK/SetTCLK cycle to advance PC from address-2 to address
 * before the first word enters the loop.
 *
 * No dummy/priming data shift is needed — the setup ClrTCLK only increments
 * PC without committing a write because no DR_SHIFT16 through DATA_QUICK has
 * occurred yet. A previous version used a dummy DR_SHIFT16(0x1111) as a
 * "priming" write, which was incorrect: it risked a spurious write to
 * address-2, potentially violating memory protection regions.
 */
static bool sbw_begin_write_block16_quick(const uint32_t clk, const uint32_t dio, uint32_t address) {
    if (!sbw_set_pc_430xv2(clk, dio, (address - 2u) & 0x000FFFFFu)) {
        return false;
    }

    sbw_set_tclk(clk, dio);
    sbw_shift_ir8_no_capture(clk, dio, SBW_IR_CNTRL_SIG_16BIT, true);
    sbw_shift_dr16_no_capture(clk, dio, 0x0500, true);
    sbw_shift_ir8_no_capture(clk, dio, SBW_IR_ADDR_CAPTURE, true);
    sbw_shift_ir8_no_capture(clk, dio, SBW_IR_DATA_QUICK, true);

    // ClrTCLK increments PC from address-2 to address. SetTCLK restores HIGH
    // for the write loop (each word: DR at HIGH → ClrTCLK commits → SetTCLK).
    sbw_clr_tclk(clk, dio);
    sbw_set_tclk(clk, dio);
    return true;
}

static inline void sbw_write_block16_quick_word(const uint32_t clk, const uint32_t dio, uint16_t data) {
    sbw_shift_dr16_no_capture(clk, dio, data, true);
    sbw_clr_tclk(clk, dio);
    sbw_set_tclk(clk, dio);
}

static bool sbw_finish_write_block16_quick(const uint32_t clk, const uint32_t dio) {
    sbw_shift_ir8_no_capture(clk, dio, SBW_IR_CNTRL_SIG_16BIT, true);
    sbw_shift_dr16_no_capture(clk, dio, 0x0501, true);
    sbw_clr_tclk(clk, dio);
    sbw_set_tclk(clk, dio);
    return sbw_in_full_emulation(clk, dio, NULL);
}

static bool sbw_write_block16_internal(const uint32_t clk, const uint32_t dio,
    uint32_t address, const uint16_t *data, size_t word_count)
{
    if (!sbw_in_full_emulation(clk, dio, NULL)) {
        return false;
    }
    if (word_count == 0) {
        return true;
    }

    uint16_t block_mask = 0;
    uint16_t saved_fram_cfg = 0;
    bool fram_cfg_changed = false;

    if (!sbw_block_protection_mask(address, word_count, &block_mask)) {
        return false;
    }
    if (!sbw_unlock_fram_for_address(clk, dio, address, &saved_fram_cfg, &fram_cfg_changed)) {
        return false;
    }

    if (block_mask == 0) {
        for (size_t index = 0; index < word_count; ++index) {
            sbw_write_mem16_sequence(clk, dio, address + (uint32_t)(index * 2u), data[index]);
        }
        const bool write_ok = sbw_in_full_emulation(clk, dio, NULL);
        const bool restore_ok = sbw_restore_fram_protection(clk, dio, saved_fram_cfg, fram_cfg_changed);
        return write_ok && restore_ok;
    }

    if (!sbw_begin_write_block16_quick(clk, dio, address)) {
        (void)sbw_restore_fram_protection(clk, dio, saved_fram_cfg, fram_cfg_changed);
        return false;
    }

    for (size_t index = 0; index < word_count; ++index) {
        sbw_write_block16_quick_word(clk, dio, data[index]);
    }

    const bool write_ok = sbw_finish_write_block16_quick(clk, dio);
    const bool restore_ok = sbw_restore_fram_protection(clk, dio, saved_fram_cfg, fram_cfg_changed);
    return write_ok && restore_ok;
}

/* --- MicroPython native entry points --- */

static mp_obj_t sbw_make_bool_u8(bool ok, uint8_t value) {
    mp_obj_t items[2] = { mp_obj_new_bool(ok), mp_obj_new_int_from_uint(value) };
    return mp_obj_new_tuple(2, items);
}

static mp_obj_t sbw_make_bool_u16(bool ok, uint16_t value) {
    mp_obj_t items[2] = { mp_obj_new_bool(ok), mp_obj_new_int_from_uint(value) };
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
    uint32_t clk, dio;
    sbw_parse_hw(hw_in, &clk, &dio);
    sbw_hw_init(clk, dio);

    uint8_t last_id = 0;
    bool ok = false;
    for (uint32_t attempt = 0; attempt < SBW_JTAG_ATTEMPTS; ++attempt) {
        sbw_begin_session(clk, dio);
        last_id = sbw_shift_ir8_capture(clk, dio, SBW_IR_CNTRL_SIG_CAPTURE, true);
        sbw_release(clk, dio);
        if (last_id == SBW_JTAG_ID_EXPECTED) {
            ok = true;
            break;
        }
    }
    return sbw_make_bool_u8(ok, last_id);
}
static MP_DEFINE_CONST_FUN_OBJ_1(sbw_native_read_id_obj, sbw_native_read_id);

static mp_obj_t sbw_native_bypass_test(mp_obj_t hw_in) {
    uint32_t clk, dio;
    sbw_parse_hw(hw_in, &clk, &dio);
    sbw_hw_init(clk, dio);

    uint16_t captured = 0;
    bool ok = false;
    for (uint32_t attempt = 0; attempt < SBW_JTAG_ATTEMPTS; ++attempt) {
        sbw_begin_session(clk, dio);
        sbw_shift_ir8_no_capture(clk, dio, SBW_IR_BYPASS, true);
        captured = sbw_shift_dr16_capture(clk, dio, SBW_BYPASS_SMOKE_PATTERN, true);
        sbw_release(clk, dio);
        if (captured == SBW_BYPASS_SMOKE_EXPECTED) {
            ok = true;
            break;
        }
    }
    return sbw_make_bool_u16(ok, captured);
}
static MP_DEFINE_CONST_FUN_OBJ_1(sbw_native_bypass_test_obj, sbw_native_bypass_test);

static mp_obj_t sbw_native_sync_and_por(mp_obj_t hw_in) {
    uint32_t clk, dio;
    sbw_parse_hw(hw_in, &clk, &dio);
    sbw_hw_init(clk, dio);

    uint16_t capture = 0;
    bool ok = false;
    for (uint32_t attempt = 0; attempt < SBW_JTAG_ATTEMPTS; ++attempt) {
        sbw_begin_session(clk, dio);
        ok = sbw_prepare_cpu(clk, dio, &capture);
        sbw_release(clk, dio);
        if (ok) {
            break;
        }
    }
    return sbw_make_bool_u16(ok, capture);
}
static MP_DEFINE_CONST_FUN_OBJ_1(sbw_native_sync_and_por_obj, sbw_native_sync_and_por);

static mp_obj_t sbw_native_read_mem16(mp_obj_t hw_in, mp_obj_t address_in) {
    uint32_t clk, dio;
    sbw_parse_hw(hw_in, &clk, &dio);
    sbw_hw_init(clk, dio);

    const uint32_t address = (uint32_t)mp_obj_get_int_truncated(address_in);
    uint16_t data = 0;
    bool ok = false;
    for (uint32_t attempt = 0; attempt < SBW_JTAG_ATTEMPTS; ++attempt) {
        sbw_begin_session(clk, dio);
        ok = sbw_prepare_cpu(clk, dio, NULL) && sbw_read_mem16_internal(clk, dio, address, &data);
        sbw_release(clk, dio);
        if (ok) {
            break;
        }
    }
    return sbw_make_bool_u16(ok, data);
}
static MP_DEFINE_CONST_FUN_OBJ_2(sbw_native_read_mem16_obj, sbw_native_read_mem16);

static mp_obj_t sbw_native_read_block16(mp_obj_t hw_in, mp_obj_t address_in, mp_obj_t words_in) {
    uint32_t clk, dio;
    sbw_parse_hw(hw_in, &clk, &dio);
    sbw_hw_init(clk, dio);

    const uint32_t address = (uint32_t)mp_obj_get_int_truncated(address_in);
    const size_t word_count = (size_t)mp_obj_get_int_truncated(words_in);
    uint16_t *buffer = NULL;
    bool ok = false;

    if (word_count != 0) {
        buffer = (uint16_t *)m_malloc(word_count * sizeof(uint16_t));
    }

    for (uint32_t attempt = 0; attempt < SBW_JTAG_ATTEMPTS; ++attempt) {
        sbw_begin_session(clk, dio);
        ok = sbw_prepare_cpu(clk, dio, NULL) && sbw_read_block16_internal(clk, dio, address, buffer, word_count);
        sbw_release(clk, dio);
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
static MP_DEFINE_CONST_FUN_OBJ_3(sbw_native_read_block16_obj, sbw_native_read_block16);

static mp_obj_t sbw_native_write_mem16(mp_obj_t hw_in, mp_obj_t address_in, mp_obj_t value_in) {
    uint32_t clk, dio;
    sbw_parse_hw(hw_in, &clk, &dio);
    sbw_hw_init(clk, dio);

    const uint32_t address = (uint32_t)mp_obj_get_int_truncated(address_in);
    const uint16_t value = (uint16_t)mp_obj_get_int_truncated(value_in);
    uint16_t readback = 0;
    bool ok = false;
    for (uint32_t attempt = 0; attempt < SBW_JTAG_ATTEMPTS; ++attempt) {
        sbw_begin_session(clk, dio);
        ok = sbw_prepare_cpu(clk, dio, NULL) &&
            sbw_write_target_word(clk, dio, address, value) &&
            sbw_read_mem16_internal(clk, dio, address, &readback) &&
            readback == value;
        sbw_release(clk, dio);
        if (ok) {
            break;
        }
    }
    return sbw_make_bool_u16(ok, readback);
}
static MP_DEFINE_CONST_FUN_OBJ_3(sbw_native_write_mem16_obj, sbw_native_write_mem16);

static mp_obj_t sbw_native_write_block16(mp_obj_t hw_in, mp_obj_t address_in, mp_obj_t data_in) {
    uint32_t clk, dio;
    sbw_parse_hw(hw_in, &clk, &dio);
    sbw_hw_init(clk, dio);

    mp_buffer_info_t bufinfo;
    const uint32_t address = (uint32_t)mp_obj_get_int_truncated(address_in);
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
        sbw_begin_session(clk, dio);
        ok = sbw_prepare_cpu(clk, dio, NULL) && sbw_write_block16_internal(clk, dio, address, words, word_count);
        sbw_release(clk, dio);
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
