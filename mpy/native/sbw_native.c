#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "py/dynruntime.h"

enum {
    SBW_SYS_CLK_HZ = 150000000u,
    SBW_CYCLES_PER_US = SBW_SYS_CLK_HZ / 1000000u,
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
    SBW_SAFE_FRAM_PC = 0x0004,
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

typedef struct {
    volatile uint32_t *gpio_in;
    volatile uint32_t *gpio_out_set;
    volatile uint32_t *gpio_out_clr;
    volatile uint32_t *gpio_oe_set;
    volatile uint32_t *gpio_oe_clr;
    uint32_t clock_mask;
    uint32_t data_mask;
} sbw_hw_desc_t;

typedef struct {
    sbw_hw_desc_t hw;
    bool tclk_high;
} sbw_ctx_t;

enum {
    SBW_PIN_CLOCK = 2u,
    SBW_PIN_DATA = 3u,
    SBW_GPIO_CTRL_FUNCSEL_MASK = 0x1Fu,
    SBW_GPIO_FUNCSEL_SIO = 0x05u,
    SBW_GPIO_FUNCSEL_PIO0 = 0x06u,
    SBW_IO_BANK0_BASE = 0x40028000u,
    SBW_PIO0_BASE = 0x50200000u,
    SBW_PIO_SM = 0u,
    SBW_PIO_TX_WORDS_PER_PAYLOAD = 3u,
    SBW_PIO_CONTROL_BITS_PER_FIFO_WORD = 16u,
    SBW_PIO_CONTROL_BITS_PER_WORD = 44u,
    SBW_PIO_SM_HZ = 100000000u,
    SBW_PIO_CLKDIV_INT = 1u,
    SBW_PIO_CLKDIV_FRAC = 128u,
    SBW_PIO_DELAY_50NS = 4u,
    SBW_PIO_DELAY_40NS = 3u,
    SBW_PIO_DELAY_20NS = 1u,
    SBW_PIO_DELAY_30NS = 2u,
    SBW_PIO_DELAY_10NS = 0u,
    SBW_PIO_QUICK_PROG_PULL = 0u,
    SBW_PIO_QUICK_PROG_BITLOOP = 3u,
    SBW_PIO_QUICK_PROG_WRAP_TOP = 27u,
    SBW_PIO_TEST_PROG_PULL = 0u,
    SBW_PIO_TEST_PROG_BITLOOP = 2u,
    SBW_PIO_TEST_PROG_WRAP_TOP = 9u,
    SBW_PIO_PACKET_PROG_LOOP = 0u,
    SBW_PIO_PACKET_PROG_ACTIVE = 4u,
    SBW_PIO_PACKET_PROG_HELD_HI = 9u,
    SBW_PIO_PACKET_PROG_TMSLDH = 13u,
    SBW_PIO_PACKET_PROG_SLOT2 = 14u,
    SBW_PIO_PACKET_PROG_WRAP_TOP = 23u,
    SBW_PIO_CLOCK_PROG_WRAP_BOTTOM = 0u,
    SBW_PIO_CLOCK_PROG_WRAP_TOP = 1u,
    SBW_PIO_CLOCK_DATA_PROG_WRAP_BOTTOM = 0u,
    SBW_PIO_CLOCK_DATA_PROG_WRAP_TOP = 1u,
    SBW_PIO_CTRL_SM_ENABLE_BIT = 1u << SBW_PIO_SM,
    SBW_PIO_CTRL_SM_RESTART_BIT = 1u << (4u + SBW_PIO_SM),
    SBW_PIO_CTRL_CLKDIV_RESTART_BIT = 1u << (8u + SBW_PIO_SM),
    SBW_PIO_FSTAT_RXFULL_BIT = 1u << SBW_PIO_SM,
    SBW_PIO_FSTAT_RXEMPTY_BIT = 1u << (8u + SBW_PIO_SM),
    SBW_PIO_FSTAT_TXFULL_BIT = 1u << (16u + SBW_PIO_SM),
    SBW_PIO_FSTAT_TXEMPTY_BIT = 1u << (24u + SBW_PIO_SM),
    SBW_PIO_FDEBUG_TXOVER_BIT = 1u << (16u + SBW_PIO_SM),
};

enum {
    SBW_PIO_CTRL_OFFSET = 0x000u,
    SBW_PIO_FSTAT_OFFSET = 0x004u,
    SBW_PIO_FDEBUG_OFFSET = 0x008u,
    SBW_PIO_TXF0_OFFSET = 0x010u,
    SBW_PIO_RXF0_OFFSET = 0x020u,
    SBW_PIO_INSTR_MEM0_OFFSET = 0x048u,
    SBW_PIO_SM0_CLKDIV_OFFSET = 0x0C8u,
    SBW_PIO_SM0_EXECCTRL_OFFSET = 0x0CCu,
    SBW_PIO_SM0_SHIFTCTRL_OFFSET = 0x0D0u,
    SBW_PIO_SM0_ADDR_OFFSET = 0x0D4u,
    SBW_PIO_SM0_INSTR_OFFSET = 0x0D8u,
    SBW_PIO_SM0_PINCTRL_OFFSET = 0x0DCu,
};

enum {
    SBW_PIO_EXECCTRL_WRAP_TOP_LSB = 12u,
    SBW_PIO_EXECCTRL_WRAP_BOTTOM_LSB = 7u,
    SBW_PIO_EXECCTRL_JMP_PIN_LSB = 24u,
    SBW_PIO_SHIFTCTRL_PUSH_THRESH_LSB = 20u,
    SBW_PIO_SHIFTCTRL_PULL_THRESH_LSB = 25u,
    SBW_PIO_SHIFTCTRL_FJOIN_TX_LSB = 30u,
    SBW_PIO_SHIFTCTRL_OUT_SHIFTDIR_LSB = 19u,
    SBW_PIO_SHIFTCTRL_IN_SHIFTDIR_LSB = 18u,
    SBW_PIO_SHIFTCTRL_AUTOPULL_LSB = 17u,
    SBW_PIO_SHIFTCTRL_AUTOPUSH_LSB = 16u,
    SBW_PIO_PINCTRL_IN_BASE_LSB = 15u,
    SBW_PIO_PINCTRL_SIDESET_COUNT_LSB = 29u,
    SBW_PIO_PINCTRL_SET_COUNT_LSB = 26u,
    SBW_PIO_PINCTRL_OUT_COUNT_LSB = 20u,
    SBW_PIO_PINCTRL_SIDESET_BASE_LSB = 10u,
    SBW_PIO_PINCTRL_SET_BASE_LSB = 5u,
    SBW_PIO_PINCTRL_OUT_BASE_LSB = 0u,
};

enum {
    SBW_PIO_INSTR_BITS_JMP = 0x0000u,
    SBW_PIO_INSTR_BITS_IN = 0x4000u,
    SBW_PIO_INSTR_BITS_OUT = 0x6000u,
    SBW_PIO_INSTR_BITS_PULL = 0x8080u,
    SBW_PIO_INSTR_BITS_PUSH = 0x8000u,
    SBW_PIO_INSTR_BITS_MOV = 0xA000u,
    SBW_PIO_INSTR_BITS_SET = 0xE000u,
    SBW_PIO_SRC_DEST_PINS = 0u,
    SBW_PIO_SRC_DEST_X = 1u,
    SBW_PIO_SRC_DEST_Y = 2u,
    SBW_PIO_SRC_DEST_NULL = 3u,
    SBW_PIO_SRC_DEST_PINDIRS = 4u,
};

static inline void sbw_mmio_write(volatile uint32_t *addr, uint32_t value) {
    *addr = value;
}

static inline uint32_t sbw_mmio_read(volatile uint32_t *addr) {
    return *addr;
}

static inline volatile uint32_t *sbw_mmio_ptr(uint32_t address) {
    return (volatile uint32_t *)(uintptr_t)address;
}

#define SBW_PIO_ENCODE_INSTR_AND_ARGS(bits, arg1, arg2) ((uint16_t)((bits) | (((arg1) & 0x7u) << 5u) | ((arg2) & 0x1Fu)))
#define SBW_PIO_ENCODE_PULL_BLOCK() SBW_PIO_ENCODE_INSTR_AND_ARGS(SBW_PIO_INSTR_BITS_PULL, 0x1u, 0x0u)
#define SBW_PIO_ENCODE_PUSH_BLOCK() SBW_PIO_ENCODE_INSTR_AND_ARGS(SBW_PIO_INSTR_BITS_PUSH, 0x1u, 0x0u)
#define SBW_PIO_ENCODE_PUSH_NOBLOCK() SBW_PIO_ENCODE_INSTR_AND_ARGS(SBW_PIO_INSTR_BITS_PUSH, 0x0u, 0x0u)
#define SBW_PIO_ENCODE_JMP(addr) SBW_PIO_ENCODE_INSTR_AND_ARGS(SBW_PIO_INSTR_BITS_JMP, 0x0u, (addr))
#define SBW_PIO_ENCODE_IN(src, count) SBW_PIO_ENCODE_INSTR_AND_ARGS(SBW_PIO_INSTR_BITS_IN, (src), (count))
#define SBW_PIO_ENCODE_SET(dest, value) SBW_PIO_ENCODE_INSTR_AND_ARGS(SBW_PIO_INSTR_BITS_SET, (dest), (value))
#define SBW_PIO_ENCODE_OUT(dest, count) SBW_PIO_ENCODE_INSTR_AND_ARGS(SBW_PIO_INSTR_BITS_OUT, (dest), (count))
#define SBW_PIO_ENCODE_MOV(dest, src) SBW_PIO_ENCODE_INSTR_AND_ARGS(SBW_PIO_INSTR_BITS_MOV, (dest), (src))
#define SBW_PIO_ENCODE_JMP_X_DEC(addr) SBW_PIO_ENCODE_INSTR_AND_ARGS(SBW_PIO_INSTR_BITS_JMP, 0x2u, (addr))
#define SBW_PIO_ENCODE_JMP_Y_DEC(addr) SBW_PIO_ENCODE_INSTR_AND_ARGS(SBW_PIO_INSTR_BITS_JMP, 0x4u, (addr))
#define SBW_PIO_ENCODE_NOP() SBW_PIO_ENCODE_MOV(SBW_PIO_SRC_DEST_Y, SBW_PIO_SRC_DEST_Y)
#define SBW_PIO_SIDESET_DELAY(clock_high, delay) ((uint16_t)((((clock_high) ? 1u : 0u) << 12u) | (((delay) & 0xFu) << 8u)))

static inline volatile uint32_t *sbw_pio_reg(uint32_t offset) {
    return sbw_mmio_ptr(SBW_PIO0_BASE + offset);
}

static inline volatile uint32_t *sbw_gpio_ctrl_reg(uint32_t pin) {
    return sbw_mmio_ptr(SBW_IO_BANK0_BASE + 4u + (pin * 8u));
}

static inline void sbw_gpio_set_func(uint32_t pin, uint32_t funcsel) {
    volatile uint32_t *const ctrl = sbw_gpio_ctrl_reg(pin);
    const uint32_t value = (sbw_mmio_read(ctrl) & ~SBW_GPIO_CTRL_FUNCSEL_MASK) | (funcsel & SBW_GPIO_CTRL_FUNCSEL_MASK);
    sbw_mmio_write(ctrl, value);
}

static inline uint16_t sbw_pio_encode_instr_and_args(uint16_t instr_bits, uint8_t arg1, uint8_t arg2) {
    return (uint16_t)(instr_bits | ((uint16_t)(arg1 & 0x7u) << 5u) | (uint16_t)(arg2 & 0x1Fu));
}

static inline uint16_t sbw_pio_encode_pull_block(void) {
    return sbw_pio_encode_instr_and_args(SBW_PIO_INSTR_BITS_PULL, 0x1u, 0x0u);
}

static inline uint16_t sbw_pio_encode_set(uint8_t dest, uint8_t value) {
    return sbw_pio_encode_instr_and_args(SBW_PIO_INSTR_BITS_SET, dest, value);
}

static inline uint16_t sbw_pio_encode_out(uint8_t dest, uint8_t count) {
    return sbw_pio_encode_instr_and_args(SBW_PIO_INSTR_BITS_OUT, dest, count);
}

static inline uint16_t sbw_pio_encode_mov(uint8_t dest, uint8_t src) {
    return sbw_pio_encode_instr_and_args(SBW_PIO_INSTR_BITS_MOV, dest, src);
}

static inline uint16_t sbw_pio_encode_jmp_x_dec(uint8_t addr) {
    return sbw_pio_encode_instr_and_args(SBW_PIO_INSTR_BITS_JMP, 0x2u, addr);
}

static inline uint16_t sbw_pio_encode_nop(void) {
    return sbw_pio_encode_mov(SBW_PIO_SRC_DEST_Y, SBW_PIO_SRC_DEST_Y);
}

static inline uint16_t sbw_pio_sideset_delay(bool clock_high, uint8_t delay) {
    return (uint16_t)(((clock_high ? 1u : 0u) << 12u) | ((uint16_t)(delay & 0xFu) << 8u));
}

/*
 * PIO quick-write streamer for the steady-state FRAM write body.
 *
 * Corresponding PIO assembly, using one sideset bit on SBWTCK:
 *
 *   ; Chunk contract:
 *   ;   bits[4:0]   = loop count for this chunk minus 1
 *   ;   bits[20:5]  = up to 16 control bits, consumed LSB-first
 *   ; Every logical SBW bit consumes two control bits:
 *   ;   control[0] = TMS level for slot 1
 *   ;   control[1] = TDI level for slot 2
 *   ;
 *   ; Y starts at 2, so we consume three chunks per MSP430 word:
 *   ;   chunk 0: go_to_shift_dr + first 5 DR bits
 *   ;   chunk 1: next 8 DR bits
 *   ;   chunk 2: last 3 DR bits + finish_shift
 *   ;
 *   ; The TCLK transitions after finish_shift are fixed and are hardcoded
 *   ; below. This keeps the streamed control bits focused on:
 *   ;   go_to_shift_dr
 *   ;   DR_SHIFT16 (no capture)
 *   ;   finish_shift
 *
 *   set    y, 2          side 1
 * chunk:
 *   pull   block         side 1
 *   out    x, 5          side 1     ; x = chunk bit count minus 1
 * bitloop:
 *   out    pins, 1       side 1 [4] ; slot 1: drive TMS
 *   nop                  side 0 [4]
 *   out    pins, 1       side 1 [4] ; slot 2: drive TDI
 *   nop                  side 0 [4]
 *   set    pindirs, 0    side 1 [4] ; slot 3: release for TDO
 *   nop                  side 0 [4]
 *   set    pindirs, 1    side 1     ; reacquire immediately after rising edge
 *   jmp    x--, bitloop  side 1
 *   jmp    y--, chunk    side 1     ; more chunk payload still needed?
 *
 *   ; Fixed tail after finish_shift:
 *   ; ClrTCLK: special TMSLDH slot, then TDI low, then released TDO slot
 *   set    pins, 0       side 1 [4]
 *   nop                  side 0 [1]
 *   set    pins, 1       side 0 [2]
 *   nop                  side 1
 *   set    pins, 0       side 1 [4]
 *   nop                  side 0 [4]
 *   set    pindirs, 0    side 1 [4]
 *   nop                  side 0 [4]
 *   set    pindirs, 1    side 1
 *
 *   ; SetTCLK: normal slot 1 low, slot 2 high, slot 3 released
 *   set    pins, 0       side 1 [4]
 *   nop                  side 0 [4]
 *   set    pins, 1       side 1 [4]
 *   nop                  side 0 [4]
 *   set    pindirs, 0    side 1 [4]
 *   nop                  side 0 [4]
 *   set    pindirs, 1    side 1
 *
 * Human-readable behavior:
 * - The state machine runs at 100 MHz, so the standard slot shape is an
 *   exact 50 ns high / 50 ns low on SBWTCK.
 * - During the streamed portion, the CPU provides only logical JTAG bits.
 *   The PIO owns the full SBW waveform shape for each logical bit:
 *   drive TMS, drive TDI, release for TDO, then re-drive immediately.
 * - The post-shift ClrTCLK/SetTCLK pair is fixed for every streamed word, so
 *   it is encoded directly in the program instead of spending FIFO bandwidth
 *   on those same control bits repeatedly.
 */
static const uint16_t sbw_pio_quick_write_program[] = {
    // set y, 2 side 1
    (uint16_t)(SBW_PIO_ENCODE_SET(SBW_PIO_SRC_DEST_Y, 2u) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_10NS)),
    // pull block side 1
    (uint16_t)(SBW_PIO_ENCODE_PULL_BLOCK() | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_10NS)),
    // out x, 5 side 1
    (uint16_t)(SBW_PIO_ENCODE_OUT(SBW_PIO_SRC_DEST_X, 5u) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_10NS)),
    // out pins, 1 side 1 [4]
    (uint16_t)(SBW_PIO_ENCODE_OUT(SBW_PIO_SRC_DEST_PINS, 1u) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_50NS)),
    // nop side 0 [4]
    (uint16_t)(SBW_PIO_ENCODE_NOP() | SBW_PIO_SIDESET_DELAY(false, SBW_PIO_DELAY_50NS)),
    // out pins, 1 side 1 [4]
    (uint16_t)(SBW_PIO_ENCODE_OUT(SBW_PIO_SRC_DEST_PINS, 1u) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_50NS)),
    // nop side 0 [4]
    (uint16_t)(SBW_PIO_ENCODE_NOP() | SBW_PIO_SIDESET_DELAY(false, SBW_PIO_DELAY_50NS)),
    // set pindirs, 0 side 1 [4]
    (uint16_t)(SBW_PIO_ENCODE_SET(SBW_PIO_SRC_DEST_PINDIRS, 0u) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_50NS)),
    // nop side 0 [4]
    (uint16_t)(SBW_PIO_ENCODE_NOP() | SBW_PIO_SIDESET_DELAY(false, SBW_PIO_DELAY_50NS)),
    // set pindirs, 1 side 1
    (uint16_t)(SBW_PIO_ENCODE_SET(SBW_PIO_SRC_DEST_PINDIRS, 1u) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_10NS)),
    // jmp x--, bitloop side 1
    (uint16_t)(SBW_PIO_ENCODE_JMP_X_DEC(SBW_PIO_QUICK_PROG_BITLOOP) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_10NS)),
    // jmp y--, chunk side 1
    (uint16_t)(SBW_PIO_ENCODE_JMP_Y_DEC(1u) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_10NS)),

    // Fixed ClrTCLK tail.
    // set pins, 0 side 1 [4]
    (uint16_t)(SBW_PIO_ENCODE_SET(SBW_PIO_SRC_DEST_PINS, 0u) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_50NS)),
    // nop side 0 [1]
    (uint16_t)(SBW_PIO_ENCODE_NOP() | SBW_PIO_SIDESET_DELAY(false, SBW_PIO_DELAY_20NS)),
    // set pins, 1 side 0 [2]
    (uint16_t)(SBW_PIO_ENCODE_SET(SBW_PIO_SRC_DEST_PINS, 1u) | SBW_PIO_SIDESET_DELAY(false, SBW_PIO_DELAY_30NS)),
    // nop side 1
    (uint16_t)(SBW_PIO_ENCODE_NOP() | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_10NS)),
    // set pins, 0 side 1 [4]
    (uint16_t)(SBW_PIO_ENCODE_SET(SBW_PIO_SRC_DEST_PINS, 0u) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_50NS)),
    // nop side 0 [4]
    (uint16_t)(SBW_PIO_ENCODE_NOP() | SBW_PIO_SIDESET_DELAY(false, SBW_PIO_DELAY_50NS)),
    // set pindirs, 0 side 1 [4]
    (uint16_t)(SBW_PIO_ENCODE_SET(SBW_PIO_SRC_DEST_PINDIRS, 0u) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_50NS)),
    // nop side 0 [4]
    (uint16_t)(SBW_PIO_ENCODE_NOP() | SBW_PIO_SIDESET_DELAY(false, SBW_PIO_DELAY_50NS)),
    // set pindirs, 1 side 1
    (uint16_t)(SBW_PIO_ENCODE_SET(SBW_PIO_SRC_DEST_PINDIRS, 1u) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_10NS)),

    // Fixed SetTCLK tail.
    // set pins, 0 side 1 [4]
    (uint16_t)(SBW_PIO_ENCODE_SET(SBW_PIO_SRC_DEST_PINS, 0u) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_50NS)),
    // nop side 0 [4]
    (uint16_t)(SBW_PIO_ENCODE_NOP() | SBW_PIO_SIDESET_DELAY(false, SBW_PIO_DELAY_50NS)),
    // set pins, 1 side 1 [4]
    (uint16_t)(SBW_PIO_ENCODE_SET(SBW_PIO_SRC_DEST_PINS, 1u) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_50NS)),
    // nop side 0 [4]
    (uint16_t)(SBW_PIO_ENCODE_NOP() | SBW_PIO_SIDESET_DELAY(false, SBW_PIO_DELAY_50NS)),
    // set pindirs, 0 side 1 [4]
    (uint16_t)(SBW_PIO_ENCODE_SET(SBW_PIO_SRC_DEST_PINDIRS, 0u) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_50NS)),
    // nop side 0 [4]
    (uint16_t)(SBW_PIO_ENCODE_NOP() | SBW_PIO_SIDESET_DELAY(false, SBW_PIO_DELAY_50NS)),
    // set pindirs, 1 side 1
    (uint16_t)(SBW_PIO_ENCODE_SET(SBW_PIO_SRC_DEST_PINDIRS, 1u) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_10NS)),
};

/*
 * Standalone PIO pattern streamer for scope/debug work.
 *
 * This program does not know anything about the MSP430 TAP or TCLK semantics.
 * It simply emits raw logical SBW bits with the normal slot shape:
 *   slot 1 = driven TMS
 *   slot 2 = driven TDI
 *   slot 3 = released TDO window, then immediate re-drive
 *
 * Corresponding PIO assembly:
 *
 *   ; Each FIFO word contributes one independently-sized chunk.
 *   ; bits[4:0]   = logical bit count minus 1
 *   ; bits[6:5]   = logical bit 0  (TMS, TDI)
 *   ; bits[8:7]   = logical bit 1
 *   ; ...
 *   ; bits[30:29] = logical bit 12
 *   ;
 *   ; Control bits are consumed LSB-first.
 *
 *   pull   block         side 1
 *   out    x, 5          side 1
 * bitloop:
 *   out    pins, 1       side 1 [4] ; slot 1: drive TMS
 *   nop                  side 0 [4]
 *   out    pins, 1       side 1 [4] ; slot 2: drive TDI
 *   nop                  side 0 [4]
 *   set    pindirs, 0    side 1 [4] ; slot 3: release data
 *   nop                  side 0 [4]
 *   set    pindirs, 1    side 1 [4] ; reacquire immediately
 *   jmp    x--, bitloop  side 1
 *
 * Human-readable behavior:
 * - The state machine still runs at 100 MHz with a 10 MHz SBWTCK slot rate.
 * - Every logical bit is independent; there is no JTAG state-machine meaning.
 * - This is meant purely for checking PIO handoff and raw waveform generation
 *   on GP2/GP3 without involving the target device.
 */
static const uint16_t sbw_pio_test_pattern_program[] = {
    // pull block side 1
    (uint16_t)(SBW_PIO_ENCODE_PULL_BLOCK() | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_10NS)),
    // out x, 5 side 1
    (uint16_t)(SBW_PIO_ENCODE_OUT(SBW_PIO_SRC_DEST_X, 5u) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_10NS)),
    // out pins, 1 side 1 [4]
    (uint16_t)(SBW_PIO_ENCODE_OUT(SBW_PIO_SRC_DEST_PINS, 1u) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_50NS)),
    // nop side 0 [4]
    (uint16_t)(SBW_PIO_ENCODE_NOP() | SBW_PIO_SIDESET_DELAY(false, SBW_PIO_DELAY_50NS)),
    // out pins, 1 side 1 [4]
    (uint16_t)(SBW_PIO_ENCODE_OUT(SBW_PIO_SRC_DEST_PINS, 1u) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_50NS)),
    // nop side 0 [4]
    (uint16_t)(SBW_PIO_ENCODE_NOP() | SBW_PIO_SIDESET_DELAY(false, SBW_PIO_DELAY_50NS)),
    // set pindirs, 0 side 1 [4]
    (uint16_t)(SBW_PIO_ENCODE_SET(SBW_PIO_SRC_DEST_PINDIRS, 0u) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_50NS)),
    // nop side 0 [4]
    (uint16_t)(SBW_PIO_ENCODE_NOP() | SBW_PIO_SIDESET_DELAY(false, SBW_PIO_DELAY_50NS)),
    // set pindirs, 1 side 1 [4]
    (uint16_t)(SBW_PIO_ENCODE_SET(SBW_PIO_SRC_DEST_PINDIRS, 1u) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_50NS)),
    // jmp x--, bitloop side 1
    (uint16_t)(SBW_PIO_ENCODE_JMP_X_DEC(SBW_PIO_TEST_PROG_BITLOOP) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_10NS)),
};

/*
 * Packet-driven SBW streamer.
 *
 * Each 32-bit TX FIFO word holds eight logical SBW packet nibbles in LSB-first
 * order. Each nibble uses the layout:
 *
 *   bit 0: DONE
 *   bit 1: TMS
 *   bit 2: TDI1
 *   bit 3: TDI2 / held level after the TDO slot
 *
 * DONE means "no more logical packets in this TX word". When DONE=1:
 * - the current ISR contents are pushed to RX FIFO with PUSH BLOCK
 * - the remaining 31 bits in the current OSR word are discarded
 * - the next OUT restarts cleanly at the next TX FIFO word
 *
 * Full 8-packet TX words do not need an explicit push. Instead, each logical
 * packet contributes a whole 4-bit nibble into the ISR:
 *
 *   in null, 3
 *   in pins, 1
 *
 * With AUTOPUSH enabled at 32 bits, eight logical packets naturally flush one
 * RX word. Partial words use the explicit DONE push. The sender can therefore
 * prove completion by reading exactly as many RX words as it wrote TX words.
 *
 * Pseudo-ASM:
 *
 *   loop:
 *     out    x, 1          side 1      ; X = DONE
 *     jmp    !x, active    side 1      ; DONE=0 means this nibble is a real packet
 *     push   block         side 1      ; partial TX word flushes here
 *     out    null, 31      side 1      ; discard the rest of this TX word
 *     jmp    loop          side 1      ; next OUT AUTOPULLs the next TX word
 *
 *   active:
 *     out    x, 1          side 1      ; X = TMS
 *     jmp    pin, held_hi  side 1      ; branch on current held SBWTDIO level
 *
 *   held_lo:
 *     mov    pins, x       side 1 [4]  ; slot 1 high phase
 *     nop                  side 0 [4]  ; slot 1 low phase
 *     jmp    slot2         side 1
 *
 *   held_hi:
 *     mov    pins, x       side 1 [2]  ; keep requested TMS during slot-1 high phase
 *     jmp    !x, tmsldh    side 0 [1]  ; held-high + TMS=0 requires TMSLDH
 *     nop                  side 0 [2]  ; held-high + TMS=1 is a normal low phase
 *     jmp    slot2         side 1
 *
 *   tmsldh:
 *     set    pins, 1       side 0 [2]  ; raise data during the slot-1 low phase
 *
 *   slot2:
 *     out    pins, 1       side 1 [4]  ; slot 2: drive TDI1
 *     nop                  side 0 [4]
 *     set    pindirs, 0    side 1      ; slot 3: release for TDO
 *     nop                  side 0 [4]
 *     nop                  side 0 [4]
 *     in     null, 3       side 1      ; reserve 3 low bits in this nibble
 *     in     pins, 1       side 1      ; capture TDO into the nibble LSB
 *     out    pins, 1       side 1      ; preload TDI2 while still input
 *     set    pindirs, 1    side 1      ; reacquire immediately after the sample
 *     jmp    loop          side 1
 */
static const uint16_t sbw_pio_packet_program[] = {
    // 0: out x, 1 side 1        ; X = DONE for this packet nibble
    (uint16_t)(SBW_PIO_ENCODE_OUT(SBW_PIO_SRC_DEST_X, 1u) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_10NS)),
    // 1: jmp !x, active side 1  ; DONE=0 means this nibble carries a real logical SBW bit
    (uint16_t)(SBW_PIO_ENCODE_INSTR_AND_ARGS(SBW_PIO_INSTR_BITS_JMP, 0x1u, SBW_PIO_PACKET_PROG_ACTIVE) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_10NS)),
    // 2: push block side 1      ; DONE=1 flushes all captured TDO nibbles for this TX word
    (uint16_t)(SBW_PIO_ENCODE_PUSH_BLOCK() | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_10NS)),
    // 3: out null, 31 side 1    ; discard the rest of the current 32-bit TX word
    (uint16_t)(SBW_PIO_ENCODE_OUT(SBW_PIO_SRC_DEST_NULL, 31u) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_10NS)),

    // 4: out x, 1 side 1        ; X = TMS for slot 1
    (uint16_t)(SBW_PIO_ENCODE_OUT(SBW_PIO_SRC_DEST_X, 1u) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_10NS)),
    // 5: jmp pin, held_hi side 1 ; branch on the currently held SBWTDIO level
    (uint16_t)(SBW_PIO_ENCODE_INSTR_AND_ARGS(SBW_PIO_INSTR_BITS_JMP, 0x6u, SBW_PIO_PACKET_PROG_HELD_HI) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_10NS)),

    // 6: mov pins, x side 1 [4] ; held-low path, slot 1 high phase
    (uint16_t)(SBW_PIO_ENCODE_MOV(SBW_PIO_SRC_DEST_PINS, SBW_PIO_SRC_DEST_X) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_50NS)),
    // 7: nop side 0 [4]         ; held-low path, slot 1 low phase
    (uint16_t)(SBW_PIO_ENCODE_NOP() | SBW_PIO_SIDESET_DELAY(false, SBW_PIO_DELAY_50NS)),
    // 8: jmp slot2 side 1       ; slot 1 complete, proceed to slot 2
    (uint16_t)(SBW_PIO_ENCODE_JMP(SBW_PIO_PACKET_PROG_SLOT2) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_10NS)),

    // 9: mov pins, x side 1 [2] ; held-high path, keep requested TMS during the high phase
    (uint16_t)(SBW_PIO_ENCODE_MOV(SBW_PIO_SRC_DEST_PINS, SBW_PIO_SRC_DEST_X) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_30NS)),
    // 10: jmp !x, tmsldh side 0 [1] ; held-high + TMS=0 requires TMSLDH
    (uint16_t)(SBW_PIO_ENCODE_INSTR_AND_ARGS(SBW_PIO_INSTR_BITS_JMP, 0x1u, SBW_PIO_PACKET_PROG_TMSLDH) | SBW_PIO_SIDESET_DELAY(false, SBW_PIO_DELAY_20NS)),
    // 11: nop side 0 [2]        ; held-high + TMS=1 is a normal low phase
    (uint16_t)(SBW_PIO_ENCODE_NOP() | SBW_PIO_SIDESET_DELAY(false, SBW_PIO_DELAY_30NS)),
    // 12: jmp slot2 side 1      ; slot 1 complete, proceed to slot 2
    (uint16_t)(SBW_PIO_ENCODE_JMP(SBW_PIO_PACKET_PROG_SLOT2) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_10NS)),

    // 13: set pins, 1 side 0 [2] ; TMSLDH raises data during the slot-1 low phase
    (uint16_t)(SBW_PIO_ENCODE_SET(SBW_PIO_SRC_DEST_PINS, 1u) | SBW_PIO_SIDESET_DELAY(false, SBW_PIO_DELAY_30NS)),

    // 14: out pins, 1 side 1 [4] ; slot 2 high phase, drive TDI1
    (uint16_t)(SBW_PIO_ENCODE_OUT(SBW_PIO_SRC_DEST_PINS, 1u) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_50NS)),
    // 15: nop side 0 [4]         ; slot 2 low phase
    (uint16_t)(SBW_PIO_ENCODE_NOP() | SBW_PIO_SIDESET_DELAY(false, SBW_PIO_DELAY_50NS)),
    // 16: set pindirs, 0 side 1  ; slot 3 starts by releasing SBWTDIO
    (uint16_t)(SBW_PIO_ENCODE_SET(SBW_PIO_SRC_DEST_PINDIRS, 0u) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_10NS)),
    // 17: nop side 0 [4]         ; slot 3 low phase before the sample edge
    (uint16_t)(SBW_PIO_ENCODE_NOP() | SBW_PIO_SIDESET_DELAY(false, SBW_PIO_DELAY_50NS)),
    // 18: nop side 0 [4]         ; extra low-phase margin before the sample edge
    (uint16_t)(SBW_PIO_ENCODE_NOP() | SBW_PIO_SIDESET_DELAY(false, SBW_PIO_DELAY_50NS)),
    // 19: in null, 3 side 1      ; reserve 3 zero bits for this packet's RX nibble
    (uint16_t)(SBW_PIO_ENCODE_IN(SBW_PIO_SRC_DEST_NULL, 3u) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_10NS)),
    // 20: in pins, 1 side 1      ; capture TDO into the nibble LSB
    (uint16_t)(SBW_PIO_ENCODE_IN(SBW_PIO_SRC_DEST_PINS, 1u) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_10NS)),
    // 21: out pins, 1 side 1     ; preload TDI2 while still input
    (uint16_t)(SBW_PIO_ENCODE_OUT(SBW_PIO_SRC_DEST_PINS, 1u) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_10NS)),
    // 22: set pindirs, 1 side 1  ; reacquire immediately after the sample
    (uint16_t)(SBW_PIO_ENCODE_SET(SBW_PIO_SRC_DEST_PINDIRS, 1u) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_10NS)),
    // 23: jmp loop side 1        ; next packet nibble, AUTOPULLing the next TX word as needed
    (uint16_t)(SBW_PIO_ENCODE_JMP(SBW_PIO_PACKET_PROG_LOOP) | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_10NS)),
};

/*
 * Minimal clock-only PIO smoke test.
 *
 * This program ignores SBWTDIO entirely and only toggles SBWTCK with one
 * sideset bit. At 100 MHz SM clock, each instruction below takes 50 ns, so the
 * two-instruction wrap produces a continuous 10 MHz square wave on GP2.
 *
 *   nop  side 1 [4]
 *   nop  side 0 [4]
 */
static const uint16_t sbw_pio_clock_program[] = {
    (uint16_t)(SBW_PIO_ENCODE_NOP() | SBW_PIO_SIDESET_DELAY(true, SBW_PIO_DELAY_50NS)),
    (uint16_t)(SBW_PIO_ENCODE_NOP() | SBW_PIO_SIDESET_DELAY(false, SBW_PIO_DELAY_50NS)),
};

/*
 * Minimal clock+data PIO smoke test.
 *
 * This ignores SBW semantics completely and simply drives GP2/GP3 as a pair of
 * opposite-phase square waves:
 *   - instruction 0: GP2 high, GP3 low
 *   - instruction 1: GP2 low,  GP3 high
 *
 * With a 100 MHz state-machine clock and [4] delay on each instruction, this
 * produces 10 MHz square waves on both pins with 180 degree phase offset.
 */
static const uint16_t sbw_pio_clock_data_program[] = {
    (uint16_t)(SBW_PIO_ENCODE_SET(SBW_PIO_SRC_DEST_PINS, 0x1u) | (SBW_PIO_DELAY_50NS << 8u)),
    (uint16_t)(SBW_PIO_ENCODE_SET(SBW_PIO_SRC_DEST_PINS, 0x2u) | (SBW_PIO_DELAY_50NS << 8u)),
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

static inline void sbw_pio_load_program_words(const uint16_t *program, size_t instruction_count) {
    for (size_t index = 0; index < instruction_count; ++index) {
        sbw_mmio_write(sbw_pio_reg(SBW_PIO_INSTR_MEM0_OFFSET + (uint32_t)(index * sizeof(uint32_t))),
            program[index]);
    }
}

static inline void sbw_pio_set_enabled(bool enabled) {
    volatile uint32_t *const ctrl = sbw_pio_reg(SBW_PIO_CTRL_OFFSET);
    uint32_t value = sbw_mmio_read(ctrl);
    if (enabled) {
        value |= SBW_PIO_CTRL_SM_ENABLE_BIT;
    } else {
        value &= ~SBW_PIO_CTRL_SM_ENABLE_BIT;
    }
    sbw_mmio_write(ctrl, value);
}

static inline void sbw_pio_restart_sm(void) {
    volatile uint32_t *const ctrl = sbw_pio_reg(SBW_PIO_CTRL_OFFSET);
    const uint32_t value = sbw_mmio_read(ctrl) | SBW_PIO_CTRL_SM_RESTART_BIT | SBW_PIO_CTRL_CLKDIV_RESTART_BIT;
    sbw_mmio_write(ctrl, value);
}

static inline void sbw_pio_exec(uint16_t instr) {
    sbw_mmio_write(sbw_pio_reg(SBW_PIO_SM0_INSTR_OFFSET), instr);
}

static inline void sbw_pio_clear_fdebug(void) {
    sbw_mmio_write(sbw_pio_reg(SBW_PIO_FDEBUG_OFFSET), SBW_PIO_FDEBUG_TXOVER_BIT);
}

static inline bool sbw_pio_tx_full(void) {
    return (sbw_mmio_read(sbw_pio_reg(SBW_PIO_FSTAT_OFFSET)) & SBW_PIO_FSTAT_TXFULL_BIT) != 0;
}

static inline bool sbw_pio_tx_empty(void) {
    return (sbw_mmio_read(sbw_pio_reg(SBW_PIO_FSTAT_OFFSET)) & SBW_PIO_FSTAT_TXEMPTY_BIT) != 0;
}

static inline bool sbw_pio_rx_empty(void) {
    return (sbw_mmio_read(sbw_pio_reg(SBW_PIO_FSTAT_OFFSET)) & SBW_PIO_FSTAT_RXEMPTY_BIT) != 0;
}

static inline bool sbw_pio_tx_overflowed(void) {
    return (sbw_mmio_read(sbw_pio_reg(SBW_PIO_FDEBUG_OFFSET)) & SBW_PIO_FDEBUG_TXOVER_BIT) != 0;
}

static inline uint32_t sbw_pio_read_rx(void) {
    return sbw_mmio_read(sbw_pio_reg(SBW_PIO_RXF0_OFFSET));
}

static inline uint32_t sbw_pio_sm_addr(void) {
    return sbw_mmio_read(sbw_pio_reg(SBW_PIO_SM0_ADDR_OFFSET)) & 0x1Fu;
}

static bool sbw_pio_wait_tx_slot(void) {
    for (uint32_t spin = 0; spin < 200000u; ++spin) {
        if (!sbw_pio_tx_full()) {
            return true;
        }
    }
    return false;
}

static bool sbw_pio_write_tx(uint32_t value) {
    if (!sbw_pio_wait_tx_slot()) {
        return false;
    }
    sbw_mmio_write(sbw_pio_reg(SBW_PIO_TXF0_OFFSET), value);
    return true;
}

static inline uint32_t sbw_pio_encode_shift_threshold(uint32_t threshold) {
    return (threshold == 32u) ? 0u : (threshold & 0x1Fu);
}

static void sbw_pio_apply_config(uint8_t wrap_bottom, uint8_t wrap_top, uint8_t jmp_pin) {
    sbw_mmio_write(sbw_pio_reg(SBW_PIO_SM0_CLKDIV_OFFSET),
        (SBW_PIO_CLKDIV_INT << 16u) | (SBW_PIO_CLKDIV_FRAC << 8u));
    sbw_mmio_write(sbw_pio_reg(SBW_PIO_SM0_EXECCTRL_OFFSET),
        ((uint32_t)jmp_pin << SBW_PIO_EXECCTRL_JMP_PIN_LSB) |
        ((uint32_t)wrap_top << SBW_PIO_EXECCTRL_WRAP_TOP_LSB) |
        ((uint32_t)wrap_bottom << SBW_PIO_EXECCTRL_WRAP_BOTTOM_LSB));
    sbw_mmio_write(sbw_pio_reg(SBW_PIO_SM0_SHIFTCTRL_OFFSET),
        (1u << SBW_PIO_SHIFTCTRL_FJOIN_TX_LSB) |
        (1u << SBW_PIO_SHIFTCTRL_OUT_SHIFTDIR_LSB));
    sbw_mmio_write(sbw_pio_reg(SBW_PIO_SM0_PINCTRL_OFFSET),
        (1u << SBW_PIO_PINCTRL_SIDESET_COUNT_LSB) |
        (1u << SBW_PIO_PINCTRL_SET_COUNT_LSB) |
        (1u << SBW_PIO_PINCTRL_OUT_COUNT_LSB) |
        (SBW_PIN_CLOCK << SBW_PIO_PINCTRL_SIDESET_BASE_LSB) |
        (SBW_PIN_DATA << SBW_PIO_PINCTRL_SET_BASE_LSB) |
        (SBW_PIN_DATA << SBW_PIO_PINCTRL_OUT_BASE_LSB));
}

static void sbw_pio_apply_clock_only_config(uint8_t wrap_bottom, uint8_t wrap_top) {
    sbw_mmio_write(sbw_pio_reg(SBW_PIO_SM0_CLKDIV_OFFSET),
        (SBW_PIO_CLKDIV_INT << 16u) | (SBW_PIO_CLKDIV_FRAC << 8u));
    sbw_mmio_write(sbw_pio_reg(SBW_PIO_SM0_EXECCTRL_OFFSET),
        ((uint32_t)wrap_top << SBW_PIO_EXECCTRL_WRAP_TOP_LSB) |
        ((uint32_t)wrap_bottom << SBW_PIO_EXECCTRL_WRAP_BOTTOM_LSB));
    sbw_mmio_write(sbw_pio_reg(SBW_PIO_SM0_SHIFTCTRL_OFFSET), 0u);
    sbw_mmio_write(sbw_pio_reg(SBW_PIO_SM0_PINCTRL_OFFSET),
        (1u << SBW_PIO_PINCTRL_SIDESET_COUNT_LSB) |
        (1u << SBW_PIO_PINCTRL_SET_COUNT_LSB) |
        (SBW_PIN_CLOCK << SBW_PIO_PINCTRL_SIDESET_BASE_LSB) |
        (SBW_PIN_CLOCK << SBW_PIO_PINCTRL_SET_BASE_LSB));
}

static void sbw_pio_apply_clock_data_config(uint8_t wrap_bottom, uint8_t wrap_top) {
    sbw_mmio_write(sbw_pio_reg(SBW_PIO_SM0_CLKDIV_OFFSET),
        (SBW_PIO_CLKDIV_INT << 16u) | (SBW_PIO_CLKDIV_FRAC << 8u));
    sbw_mmio_write(sbw_pio_reg(SBW_PIO_SM0_EXECCTRL_OFFSET),
        ((uint32_t)wrap_top << SBW_PIO_EXECCTRL_WRAP_TOP_LSB) |
        ((uint32_t)wrap_bottom << SBW_PIO_EXECCTRL_WRAP_BOTTOM_LSB));
    sbw_mmio_write(sbw_pio_reg(SBW_PIO_SM0_SHIFTCTRL_OFFSET), 0u);
    sbw_mmio_write(sbw_pio_reg(SBW_PIO_SM0_PINCTRL_OFFSET),
        (2u << SBW_PIO_PINCTRL_SET_COUNT_LSB) |
        (SBW_PIN_CLOCK << SBW_PIO_PINCTRL_SET_BASE_LSB));
}

static void sbw_pio_apply_packet_config(uint8_t wrap_bottom, uint8_t wrap_top, uint8_t jmp_pin) {
    sbw_mmio_write(sbw_pio_reg(SBW_PIO_SM0_CLKDIV_OFFSET),
        (SBW_PIO_CLKDIV_INT << 16u) | (SBW_PIO_CLKDIV_FRAC << 8u));
    sbw_mmio_write(sbw_pio_reg(SBW_PIO_SM0_EXECCTRL_OFFSET),
        ((uint32_t)jmp_pin << SBW_PIO_EXECCTRL_JMP_PIN_LSB) |
        ((uint32_t)wrap_top << SBW_PIO_EXECCTRL_WRAP_TOP_LSB) |
        ((uint32_t)wrap_bottom << SBW_PIO_EXECCTRL_WRAP_BOTTOM_LSB));
    sbw_mmio_write(sbw_pio_reg(SBW_PIO_SM0_SHIFTCTRL_OFFSET),
        (sbw_pio_encode_shift_threshold(32u) << SBW_PIO_SHIFTCTRL_PULL_THRESH_LSB) |
        (sbw_pio_encode_shift_threshold(32u) << SBW_PIO_SHIFTCTRL_PUSH_THRESH_LSB) |
        (1u << SBW_PIO_SHIFTCTRL_OUT_SHIFTDIR_LSB) |
        (1u << SBW_PIO_SHIFTCTRL_AUTOPULL_LSB) |
        (1u << SBW_PIO_SHIFTCTRL_AUTOPUSH_LSB));
    sbw_mmio_write(sbw_pio_reg(SBW_PIO_SM0_PINCTRL_OFFSET),
        (1u << SBW_PIO_PINCTRL_SIDESET_COUNT_LSB) |
        (1u << SBW_PIO_PINCTRL_SET_COUNT_LSB) |
        (1u << SBW_PIO_PINCTRL_OUT_COUNT_LSB) |
        (SBW_PIN_DATA << SBW_PIO_PINCTRL_IN_BASE_LSB) |
        (SBW_PIN_CLOCK << SBW_PIO_PINCTRL_SIDESET_BASE_LSB) |
        (SBW_PIN_DATA << SBW_PIO_PINCTRL_SET_BASE_LSB) |
        (SBW_PIN_DATA << SBW_PIO_PINCTRL_OUT_BASE_LSB));
}

static void sbw_pio_prepare_pins_for_handoff(sbw_ctx_t *ctx) {
    sbw_mmio_write(ctx->hw.gpio_out_set, ctx->hw.clock_mask | ctx->hw.data_mask);
    sbw_mmio_write(ctx->hw.gpio_oe_set, ctx->hw.clock_mask | ctx->hw.data_mask);
}

static void sbw_pio_prepare_clock_pin_for_handoff(sbw_ctx_t *ctx) {
    sbw_mmio_write(ctx->hw.gpio_out_clr, ctx->hw.clock_mask);
    sbw_mmio_write(ctx->hw.gpio_oe_set, ctx->hw.clock_mask);
    sbw_mmio_write(ctx->hw.gpio_oe_clr, ctx->hw.data_mask);
}

static void sbw_pio_prepare_clock_data_pins_for_handoff(sbw_ctx_t *ctx) {
    sbw_mmio_write(ctx->hw.gpio_out_clr, ctx->hw.clock_mask | ctx->hw.data_mask);
    sbw_mmio_write(ctx->hw.gpio_oe_set, ctx->hw.clock_mask | ctx->hw.data_mask);
}

static void sbw_pio_take_pins(sbw_ctx_t *ctx, const uint16_t *program, size_t instruction_count,
    uint8_t wrap_bottom, uint8_t wrap_top, uint8_t jmp_pin) {
    sbw_pio_set_enabled(false);
    sbw_pio_load_program_words(program, instruction_count);
    sbw_pio_apply_config(wrap_bottom, wrap_top, jmp_pin);
    sbw_pio_clear_fdebug();
    sbw_pio_restart_sm();
    sbw_pio_exec((uint16_t)(sbw_pio_encode_set(SBW_PIO_SRC_DEST_PINS, 1u) | sbw_pio_sideset_delay(true, SBW_PIO_DELAY_10NS)));
    sbw_pio_exec((uint16_t)(sbw_pio_encode_set(SBW_PIO_SRC_DEST_PINDIRS, 1u) | sbw_pio_sideset_delay(true, SBW_PIO_DELAY_10NS)));
    sbw_pio_exec(SBW_PIO_ENCODE_JMP(wrap_bottom));
    sbw_pio_prepare_pins_for_handoff(ctx);
    sbw_gpio_set_func(SBW_PIN_CLOCK, SBW_GPIO_FUNCSEL_PIO0);
    sbw_gpio_set_func(SBW_PIN_DATA, SBW_GPIO_FUNCSEL_PIO0);
}

static void sbw_pio_take_packet_pins(sbw_ctx_t *ctx) {
    sbw_pio_set_enabled(false);
    sbw_pio_load_program_words(sbw_pio_packet_program,
        sizeof(sbw_pio_packet_program) / sizeof(sbw_pio_packet_program[0]));
    sbw_pio_apply_packet_config(SBW_PIO_PACKET_PROG_LOOP, SBW_PIO_PACKET_PROG_WRAP_TOP, SBW_PIN_DATA);
    sbw_pio_clear_fdebug();
    sbw_pio_restart_sm();
    sbw_pio_exec((uint16_t)(sbw_pio_encode_set(SBW_PIO_SRC_DEST_PINS, 1u) | sbw_pio_sideset_delay(true, SBW_PIO_DELAY_10NS)));
    sbw_pio_exec((uint16_t)(sbw_pio_encode_set(SBW_PIO_SRC_DEST_PINDIRS, 1u) | sbw_pio_sideset_delay(true, SBW_PIO_DELAY_10NS)));
    sbw_pio_exec(SBW_PIO_ENCODE_JMP(SBW_PIO_PACKET_PROG_LOOP));
    sbw_pio_set_enabled(true);
    sbw_pio_prepare_pins_for_handoff(ctx);
    sbw_gpio_set_func(SBW_PIN_CLOCK, SBW_GPIO_FUNCSEL_PIO0);
    sbw_gpio_set_func(SBW_PIN_DATA, SBW_GPIO_FUNCSEL_PIO0);
}

static void sbw_pio_take_clock_pin(sbw_ctx_t *ctx) {
    sbw_pio_set_enabled(false);
    sbw_pio_load_program_words(sbw_pio_clock_program,
        sizeof(sbw_pio_clock_program) / sizeof(sbw_pio_clock_program[0]));
    sbw_pio_apply_clock_only_config(SBW_PIO_CLOCK_PROG_WRAP_BOTTOM, SBW_PIO_CLOCK_PROG_WRAP_TOP);
    sbw_pio_clear_fdebug();
    sbw_pio_restart_sm();
    sbw_pio_exec((uint16_t)(sbw_pio_encode_set(SBW_PIO_SRC_DEST_PINS, 0u) | sbw_pio_sideset_delay(false, SBW_PIO_DELAY_10NS)));
    sbw_pio_exec((uint16_t)(sbw_pio_encode_set(SBW_PIO_SRC_DEST_PINDIRS, 1u) | sbw_pio_sideset_delay(false, SBW_PIO_DELAY_10NS)));
    sbw_pio_exec(SBW_PIO_ENCODE_JMP(SBW_PIO_CLOCK_PROG_WRAP_BOTTOM));
    sbw_pio_prepare_clock_pin_for_handoff(ctx);
    sbw_gpio_set_func(SBW_PIN_CLOCK, SBW_GPIO_FUNCSEL_PIO0);
}

static void sbw_pio_take_clock_data_pins(sbw_ctx_t *ctx) {
    sbw_pio_set_enabled(false);
    sbw_pio_load_program_words(sbw_pio_clock_data_program,
        sizeof(sbw_pio_clock_data_program) / sizeof(sbw_pio_clock_data_program[0]));
    sbw_pio_apply_clock_data_config(SBW_PIO_CLOCK_DATA_PROG_WRAP_BOTTOM, SBW_PIO_CLOCK_DATA_PROG_WRAP_TOP);
    sbw_pio_clear_fdebug();
    sbw_pio_restart_sm();
    sbw_pio_exec(sbw_pio_encode_set(SBW_PIO_SRC_DEST_PINS, 0u));
    sbw_pio_exec(sbw_pio_encode_set(SBW_PIO_SRC_DEST_PINDIRS, 0x3u));
    sbw_pio_exec(SBW_PIO_ENCODE_JMP(SBW_PIO_CLOCK_DATA_PROG_WRAP_BOTTOM));
    sbw_pio_prepare_clock_data_pins_for_handoff(ctx);
    sbw_gpio_set_func(SBW_PIN_CLOCK, SBW_GPIO_FUNCSEL_PIO0);
    sbw_gpio_set_func(SBW_PIN_DATA, SBW_GPIO_FUNCSEL_PIO0);
}

static void sbw_pio_release_pins(sbw_ctx_t *ctx) {
    sbw_pio_set_enabled(false);
    sbw_pio_prepare_pins_for_handoff(ctx);
    sbw_gpio_set_func(SBW_PIN_CLOCK, SBW_GPIO_FUNCSEL_SIO);
    sbw_gpio_set_func(SBW_PIN_DATA, SBW_GPIO_FUNCSEL_SIO);
}

static void sbw_pio_release_clock_pin(sbw_ctx_t *ctx) {
    sbw_pio_set_enabled(false);
    sbw_gpio_set_func(SBW_PIN_CLOCK, SBW_GPIO_FUNCSEL_SIO);
    sbw_mmio_write(ctx->hw.gpio_out_clr, ctx->hw.clock_mask);
    sbw_mmio_write(ctx->hw.gpio_oe_set, ctx->hw.clock_mask);
    sbw_mmio_write(ctx->hw.gpio_oe_clr, ctx->hw.data_mask);
}

static void sbw_pio_release_clock_data_pins(sbw_ctx_t *ctx) {
    sbw_pio_set_enabled(false);
    sbw_gpio_set_func(SBW_PIN_CLOCK, SBW_GPIO_FUNCSEL_SIO);
    sbw_gpio_set_func(SBW_PIN_DATA, SBW_GPIO_FUNCSEL_SIO);
    sbw_mmio_write(ctx->hw.gpio_out_clr, ctx->hw.clock_mask);
    sbw_mmio_write(ctx->hw.gpio_oe_set, ctx->hw.clock_mask);
    sbw_mmio_write(ctx->hw.gpio_oe_clr, ctx->hw.data_mask);
}

static inline void sbw_pio_control_append_bit(uint32_t *fifo_words, size_t *chunk_index, uint8_t *bit_pos, bool bit) {
    if (bit) {
        fifo_words[*chunk_index] |= (uint32_t)1u << *bit_pos;
    }
    ++(*bit_pos);
    if (*chunk_index < (SBW_PIO_TX_WORDS_PER_PAYLOAD - 1u) && *bit_pos == (5u + SBW_PIO_CONTROL_BITS_PER_FIFO_WORD)) {
        ++(*chunk_index);
        *bit_pos = 5u;
    }
}

static void sbw_pio_encode_write_word(uint16_t data, uint32_t *fifo_words) {
    /*
     * FIFO payload format expected by the PIO program above:
     *
     *   fifo_words[0]:
     *     bits[4:0]   = 7  -> first chunk carries 8 logical bits
     *     bits[20:5]  = 16 control bits
     *   fifo_words[1]:
     *     bits[4:0]   = 7  -> second chunk carries 8 logical bits
     *     bits[20:5]  = 16 control bits
     *   fifo_words[2]:
     *     bits[4:0]   = 4  -> third chunk carries 5 logical bits
     *     bits[20:5]  = 10 control bits actually used
     *
     * The three chunks together encode exactly 21 logical SBW bits:
     * - 3 bits of go_to_shift_dr
     * - 16 bits of DR_SHIFT16
     * - 2 bits of finish_shift
     *
     * Each logical SBW bit contributes two streamed control bits:
     * - first bit  -> slot-1 TMS level
     * - second bit -> slot-2 TDI level
     *
     * The ClrTCLK / SetTCLK waveform after finish_shift is fixed in the PIO
     * program and therefore is not represented in these FIFO words.
     */
    fifo_words[0] = 7u;
    fifo_words[1] = 7u;
    fifo_words[2] = 4u;

    size_t chunk_index = 0;
    uint8_t bit_pos = 5u;
    uint16_t shift = data;

    // go_to_shift_dr
    sbw_pio_control_append_bit(fifo_words, &chunk_index, &bit_pos, true);
    sbw_pio_control_append_bit(fifo_words, &chunk_index, &bit_pos, true);
    sbw_pio_control_append_bit(fifo_words, &chunk_index, &bit_pos, false);
    sbw_pio_control_append_bit(fifo_words, &chunk_index, &bit_pos, true);
    sbw_pio_control_append_bit(fifo_words, &chunk_index, &bit_pos, false);
    sbw_pio_control_append_bit(fifo_words, &chunk_index, &bit_pos, true);

    for (uint32_t bit = 0; bit < 15; ++bit) {
        sbw_pio_control_append_bit(fifo_words, &chunk_index, &bit_pos, false);
        sbw_pio_control_append_bit(fifo_words, &chunk_index, &bit_pos, (shift & 0x8000u) != 0);
        shift <<= 1;
    }

    // final DR bit with TMS high
    sbw_pio_control_append_bit(fifo_words, &chunk_index, &bit_pos, true);
    sbw_pio_control_append_bit(fifo_words, &chunk_index, &bit_pos, (shift & 0x8000u) != 0);

    // finish_shift
    sbw_pio_control_append_bit(fifo_words, &chunk_index, &bit_pos, true);
    sbw_pio_control_append_bit(fifo_words, &chunk_index, &bit_pos, true);
    sbw_pio_control_append_bit(fifo_words, &chunk_index, &bit_pos, false);
    sbw_pio_control_append_bit(fifo_words, &chunk_index, &bit_pos, true);
}

static bool sbw_pio_stream_write_block16_quick(sbw_ctx_t *ctx, const uint16_t *data, size_t word_count) {
    if (word_count == 0) {
        return true;
    }

    sbw_pio_take_pins(ctx, sbw_pio_quick_write_program,
        sizeof(sbw_pio_quick_write_program) / sizeof(sbw_pio_quick_write_program[0]),
        SBW_PIO_QUICK_PROG_PULL, SBW_PIO_QUICK_PROG_WRAP_TOP, 0u);
    bool ok = true;

    sbw_pio_set_enabled(true);

    for (size_t index = 0; index < word_count; ++index) {
        uint32_t fifo_words[SBW_PIO_TX_WORDS_PER_PAYLOAD];
        sbw_pio_encode_write_word(data[index], fifo_words);
        for (size_t chunk = 0; chunk < SBW_PIO_TX_WORDS_PER_PAYLOAD; ++chunk) {
            if (!sbw_pio_write_tx(fifo_words[chunk])) {
                ok = false;
                goto done;
            }
        }
    }

    bool done = false;
    const uint32_t spin_limit = 100000u + (uint32_t)(word_count * 8000u);
    for (uint32_t spin = 0; spin < spin_limit; ++spin) {
        if (sbw_pio_tx_empty()) {
            sbw_wait_cycles(SBW_NS_TO_CYCLES(20000));
            done = true;
            break;
        }
    }

    const bool overflow = sbw_pio_tx_overflowed();
    ok = done && !overflow;

done:
    sbw_pio_release_pins(ctx);
    ctx->tclk_high = true;
    return ok;
}

static bool sbw_pio_stream_test_pattern_word(sbw_ctx_t *ctx, uint32_t pattern_word, size_t fifo_word_count) {
    if (fifo_word_count == 0) {
        return true;
    }

    sbw_pio_take_pins(ctx, sbw_pio_test_pattern_program,
        sizeof(sbw_pio_test_pattern_program) / sizeof(sbw_pio_test_pattern_program[0]),
        SBW_PIO_TEST_PROG_PULL, SBW_PIO_TEST_PROG_WRAP_TOP, 0u);
    bool ok = true;

    sbw_pio_set_enabled(true);

    for (size_t index = 0; index < fifo_word_count; ++index) {
        if (!sbw_pio_write_tx(pattern_word)) {
            ok = false;
            goto done;
        }
    }

    bool done = false;
    const uint32_t spin_limit = 100000u + (uint32_t)(fifo_word_count * 1024u);
    for (uint32_t spin = 0; spin < spin_limit; ++spin) {
        if (sbw_pio_tx_empty()) {
            sbw_wait_cycles(SBW_NS_TO_CYCLES(20000));
            done = true;
            break;
        }
    }

    const bool overflow = sbw_pio_tx_overflowed();
    ok = done && !overflow;

done:
    sbw_pio_release_pins(ctx);
    ctx->tclk_high = true;
    sbw_mmio_write(ctx->hw.gpio_oe_set, ctx->hw.clock_mask);
    sbw_mmio_write(ctx->hw.gpio_oe_clr, ctx->hw.data_mask);
    sbw_mmio_write(ctx->hw.gpio_out_clr, ctx->hw.clock_mask);
    return ok;
}

static bool sbw_pio_run_packet_words(sbw_ctx_t *ctx, const uint32_t *packet_words, size_t word_count,
    size_t packet_count, uint32_t *rx_words, size_t rx_word_capacity, size_t *rx_word_count_out) {
    if (rx_word_count_out) {
        *rx_word_count_out = 0;
    }

    if (word_count == 0) {
        return true;
    }

    sbw_pio_take_packet_pins(ctx);
    bool ok = true;
    size_t tx_index = 0;
    size_t rx_count = 0;
    bool completed = false;
    const uint32_t spin_limit = 100000u + (uint32_t)(packet_count * 5000u);
    for (uint32_t spin = 0; spin < spin_limit; ++spin) {
        while (!sbw_pio_rx_empty()) {
            const uint32_t value = sbw_pio_read_rx();
            if (rx_words && rx_count < rx_word_capacity) {
                rx_words[rx_count] = value;
            }
            ++rx_count;
        }

        while (tx_index < word_count && !sbw_pio_tx_full()) {
            sbw_mmio_write(sbw_pio_reg(SBW_PIO_TXF0_OFFSET), packet_words[tx_index]);
            ++tx_index;
        }

        if (tx_index == word_count && rx_count == word_count && sbw_pio_tx_empty()) {
            sbw_wait_cycles(SBW_NS_TO_CYCLES(2000));

            while (!sbw_pio_rx_empty()) {
                const uint32_t value = sbw_pio_read_rx();
                if (rx_words && rx_count < rx_word_capacity) {
                    rx_words[rx_count] = value;
                }
                ++rx_count;
            }

            if (rx_count == word_count && sbw_pio_tx_empty()) {
                completed = true;
                break;
            }
        }
    }

    if (rx_word_count_out) {
        *rx_word_count_out = rx_count;
    }

    const bool overflow = sbw_pio_tx_overflowed();
    ok = completed && !overflow && tx_index == word_count && rx_count == word_count;

    sbw_pio_release_pins(ctx);
    ctx->tclk_high = true;
    return ok;
}

static bool sbw_pio_stream_packet_words(sbw_ctx_t *ctx, const uint32_t *packet_words, size_t word_count,
    size_t packet_count) {
    return sbw_pio_run_packet_words(ctx, packet_words, word_count, packet_count, NULL, 0u, NULL);
}

static bool sbw_parse_packet_words(mp_buffer_info_t *bufinfo, uint32_t **words_out, size_t *word_count_out, size_t *packet_count_out) {
    if ((bufinfo->len & 3u) != 0) {
        return false;
    }

    const size_t word_count = bufinfo->len / 4u;
    const uint8_t *bytes = (const uint8_t *)bufinfo->buf;
    uint32_t *words = NULL;
    size_t packet_count = 0;

    if (word_count != 0) {
        words = (uint32_t *)m_malloc(word_count * sizeof(uint32_t));
        for (size_t index = 0; index < word_count; ++index) {
            const size_t offset = index * 4u;
            words[index] =
                (uint32_t)bytes[offset] |
                ((uint32_t)bytes[offset + 1u] << 8u) |
                ((uint32_t)bytes[offset + 2u] << 16u) |
                ((uint32_t)bytes[offset + 3u] << 24u);
        }

        for (size_t index = 0; index < word_count; ++index) {
            for (size_t nibble = 0; nibble < 8u; ++nibble) {
                if (((words[index] >> (nibble * 4u)) & 0x1u) != 0u) {
                    break;
                }
                ++packet_count;
            }
        }
    }

    *words_out = words;
    *word_count_out = word_count;
    *packet_count_out = packet_count;
    return true;
}

static bool sbw_pio_clock_square_wave_us(sbw_ctx_t *ctx, uint32_t duration_us) {
    if (duration_us == 0u) {
        return true;
    }

    sbw_pio_take_clock_pin(ctx);
    sbw_pio_set_enabled(true);
    sbw_wait_cycles(duration_us * SBW_CYCLES_PER_US);
    sbw_pio_release_clock_pin(ctx);
    ctx->tclk_high = true;
    return true;
}

static bool sbw_pio_clock_data_square_wave_us(sbw_ctx_t *ctx, uint32_t duration_us) {
    if (duration_us == 0u) {
        return true;
    }

    sbw_pio_take_clock_data_pins(ctx);
    sbw_pio_set_enabled(true);
    sbw_wait_cycles(duration_us * SBW_CYCLES_PER_US);
    sbw_pio_release_clock_data_pins(ctx);
    ctx->tclk_high = true;
    return true;
}

typedef struct {
    uint32_t *words;
    size_t word_capacity;
    size_t word_count;
    size_t packet_count;
} sbw_packet_builder_t;

static void sbw_packet_builder_init(sbw_packet_builder_t *builder, uint32_t *words, size_t word_capacity) {
    builder->words = words;
    builder->word_capacity = word_capacity;
    builder->word_count = 0;
    builder->packet_count = 0;
}

static bool sbw_packet_builder_append(sbw_packet_builder_t *builder, bool tms, bool tdi1, bool tdi2) {
    const size_t word_index = builder->packet_count / 8u;
    const size_t nibble_index = builder->packet_count % 8u;
    if (word_index >= builder->word_capacity) {
        return false;
    }

    if (nibble_index == 0u) {
        builder->words[word_index] = 0u;
        builder->word_count = word_index + 1u;
    }

    uint32_t nibble =
        ((tms ? 1u : 0u) << 1u) |
        ((tdi1 ? 1u : 0u) << 2u) |
        ((tdi2 ? 1u : 0u) << 3u);
    builder->words[word_index] |= nibble << (nibble_index * 4u);
    ++builder->packet_count;
    return true;
}

static bool sbw_packet_builder_finish(sbw_packet_builder_t *builder) {
    if (builder->packet_count == 0u) {
        return false;
    }

    const size_t nibble_index = builder->packet_count % 8u;
    if (nibble_index == 0u) {
        return true;
    }

    const size_t word_index = builder->packet_count / 8u;
    builder->words[word_index] |= (uint32_t)0x1u << (nibble_index * 4u);
    return true;
}

static bool sbw_packet_builder_append_io_bit(sbw_packet_builder_t *builder, bool tms, bool tdi) {
    return sbw_packet_builder_append(builder, tms, tdi, tdi);
}

static void sbw_packet_go_to_shift_ir(sbw_packet_builder_t *builder, bool tclk_high) {
    sbw_packet_builder_append_io_bit(builder, true, tclk_high);
    sbw_packet_builder_append_io_bit(builder, true, true);
    sbw_packet_builder_append_io_bit(builder, false, true);
    sbw_packet_builder_append_io_bit(builder, false, true);
}

static void sbw_packet_finish_shift(sbw_packet_builder_t *builder, bool tclk_high) {
    sbw_packet_builder_append_io_bit(builder, true, true);
    sbw_packet_builder_append_io_bit(builder, false, tclk_high);
}

static bool sbw_packet_shift_ir8_payload_finish(sbw_packet_builder_t *builder, uint8_t instruction, bool tclk_high) {
    uint8_t shift = instruction;
    for (uint32_t bit = 0; bit < 7; ++bit) {
        if (!sbw_packet_builder_append_io_bit(builder, false, (shift & 0x1u) != 0)) {
            return false;
        }
        shift >>= 1;
    }

    if (!sbw_packet_builder_append_io_bit(builder, true, (shift & 0x1u) != 0)) {
        return false;
    }

    sbw_packet_finish_shift(builder, tclk_high);
    return true;
}

static size_t sbw_packet_word_packet_count(uint32_t packet_word) {
    for (size_t nibble = 0; nibble < 8u; ++nibble) {
        if (((packet_word >> (nibble * 4u)) & 0x1u) != 0u) {
            return nibble;
        }
    }
    return 8u;
}

static bool sbw_packet_sample_bit(const uint32_t *packet_words, size_t packet_word_count,
    const uint32_t *rx_words, size_t rx_word_count, size_t packet_index) {
    size_t remaining = packet_index;

    for (size_t word_index = 0; word_index < packet_word_count; ++word_index) {
        const size_t word_packet_count = sbw_packet_word_packet_count(packet_words[word_index]);
        if (remaining < word_packet_count) {
            if (word_index >= rx_word_count) {
                return false;
            }

            const size_t bit_index = ((word_packet_count - 1u) - remaining) * 4u;
            return ((rx_words[word_index] >> bit_index) & 0x1u) != 0u;
        }
        remaining -= word_packet_count;
    }

    return false;
}

static uint32_t sbw_packet_trace_bits(const uint32_t *packet_words, size_t packet_word_count,
    const uint32_t *rx_words, size_t rx_word_count, size_t packet_count) {
    uint32_t trace = 0u;
    const size_t trace_count = packet_count < 32u ? packet_count : 32u;
    for (size_t packet_index = 0; packet_index < trace_count; ++packet_index) {
        trace = (trace << 1u) |
            (sbw_packet_sample_bit(packet_words, packet_word_count, rx_words, rx_word_count, packet_index) ? 1u : 0u);
    }
    return trace;
}

static bool sbw_shift_ir8_capture_pio(sbw_ctx_t *ctx, uint8_t instruction, uint8_t *captured_out,
    uint32_t *rx_debug_words, size_t rx_debug_capacity, size_t *rx_debug_word_count, size_t *packet_count_out,
    uint32_t *trace_out) {
    uint32_t packet_words[2] = {0u, 0u};
    uint32_t rx_words[2] = {0u, 0u};
    sbw_packet_builder_t builder;
    sbw_packet_builder_init(&builder, packet_words, 2u);

    sbw_packet_go_to_shift_ir(&builder, ctx->tclk_high);

    uint8_t shift = instruction;
    for (uint32_t bit = 0; bit < 7; ++bit) {
        if (!sbw_packet_builder_append_io_bit(&builder, false, (shift & 0x1u) != 0)) {
            return false;
        }
        shift >>= 1;
    }

    if (!sbw_packet_builder_append_io_bit(&builder, true, (shift & 0x1u) != 0)) {
        return false;
    }
    sbw_packet_finish_shift(&builder, ctx->tclk_high);
    if (!sbw_packet_builder_finish(&builder)) {
        return false;
    }

    size_t rx_word_count = 0;
    if (!sbw_pio_run_packet_words(ctx, packet_words, builder.word_count, builder.packet_count,
            rx_words, 2u, &rx_word_count)) {
        return false;
    }

    if (rx_debug_words && rx_debug_capacity != 0u) {
        const size_t copy_count = rx_word_count < rx_debug_capacity ? rx_word_count : rx_debug_capacity;
        for (size_t index = 0; index < copy_count; ++index) {
            rx_debug_words[index] = rx_words[index];
        }
    }
    if (rx_debug_word_count) {
        *rx_debug_word_count = rx_word_count;
    }
    if (packet_count_out) {
        *packet_count_out = builder.packet_count;
    }
    if (trace_out) {
        *trace_out = sbw_packet_trace_bits(packet_words, builder.word_count, rx_words, rx_word_count, builder.packet_count);
    }

    uint8_t captured = 0u;
    for (size_t packet_index = 4u; packet_index < 12u; ++packet_index) {
        captured = (uint8_t)((captured << 1u) |
            (sbw_packet_sample_bit(packet_words, builder.word_count, rx_words, rx_word_count, packet_index) ? 1u : 0u));
    }

    if (captured_out) {
        *captured_out = captured;
    }
    return true;
}

static bool sbw_shift_ir8_no_capture_pio(sbw_ctx_t *ctx, uint8_t instruction) {
    uint32_t packet_words[2] = {0u, 0u};
    sbw_packet_builder_t builder;
    sbw_packet_builder_init(&builder, packet_words, 2u);

    sbw_packet_go_to_shift_ir(&builder, ctx->tclk_high);
    if (!sbw_packet_shift_ir8_payload_finish(&builder, instruction, ctx->tclk_high)) {
        return false;
    }
    if (!sbw_packet_builder_finish(&builder)) {
        return false;
    }

    return sbw_pio_stream_packet_words(ctx, packet_words, builder.word_count, builder.packet_count);
}

static bool sbw_go_to_shift_ir_pio(sbw_ctx_t *ctx) {
    uint32_t packet_words[1] = {0u};
    sbw_packet_builder_t builder;
    sbw_packet_builder_init(&builder, packet_words, 1u);

    sbw_packet_go_to_shift_ir(&builder, ctx->tclk_high);
    if (!sbw_packet_builder_finish(&builder)) {
        return false;
    }

    return sbw_pio_stream_packet_words(ctx, packet_words, builder.word_count, builder.packet_count);
}

static bool sbw_shift_ir8_payload_finish_pio(sbw_ctx_t *ctx, uint8_t instruction) {
    uint32_t packet_words[2] = {0u, 0u};
    sbw_packet_builder_t builder;
    sbw_packet_builder_init(&builder, packet_words, 2u);

    if (!sbw_packet_shift_ir8_payload_finish(&builder, instruction, ctx->tclk_high)) {
        return false;
    }
    if (!sbw_packet_builder_finish(&builder)) {
        return false;
    }

    return sbw_pio_stream_packet_words(ctx, packet_words, builder.word_count, builder.packet_count);
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
static void sbw_parse_hw_desc(mp_obj_t hw_in, sbw_hw_desc_t *hw) {
    size_t len = 0;
    mp_obj_t *items = NULL;
    mp_obj_get_array(hw_in, &len, &items);
    if (len != 7) {
        mp_raise_ValueError("hw tuple must have 7 items");
    }

    hw->gpio_in = sbw_mmio_ptr((uint32_t)mp_obj_get_int_truncated(items[0]));
    hw->gpio_out_set = sbw_mmio_ptr((uint32_t)mp_obj_get_int_truncated(items[1]));
    hw->gpio_out_clr = sbw_mmio_ptr((uint32_t)mp_obj_get_int_truncated(items[2]));
    hw->gpio_oe_set = sbw_mmio_ptr((uint32_t)mp_obj_get_int_truncated(items[3]));
    hw->gpio_oe_clr = sbw_mmio_ptr((uint32_t)mp_obj_get_int_truncated(items[4]));
    hw->clock_mask = (uint32_t)mp_obj_get_int_truncated(items[5]);
    hw->data_mask = (uint32_t)mp_obj_get_int_truncated(items[6]);

    if (hw->clock_mask == 0 || hw->data_mask == 0) {
        mp_raise_ValueError("hw masks must be non-zero");
    }
}

static void sbw_ctx_init(sbw_ctx_t *ctx, const sbw_hw_desc_t *hw) {
    ctx->hw = *hw;
    ctx->tclk_high = true;

    sbw_mmio_write(ctx->hw.gpio_oe_set, ctx->hw.clock_mask);
    sbw_mmio_write(ctx->hw.gpio_oe_clr, ctx->hw.data_mask);
    sbw_mmio_write(ctx->hw.gpio_out_clr, ctx->hw.clock_mask);
}

static inline void sbw_clock_drive(sbw_ctx_t *ctx, bool high) {
    if (high) {
        sbw_mmio_write(ctx->hw.gpio_out_set, ctx->hw.clock_mask);
    } else {
        sbw_mmio_write(ctx->hw.gpio_out_clr, ctx->hw.clock_mask);
    }
}

static inline void sbw_data_drive(sbw_ctx_t *ctx, bool level) {
    if (level) {
        sbw_mmio_write(ctx->hw.gpio_out_set, ctx->hw.data_mask);
    } else {
        sbw_mmio_write(ctx->hw.gpio_out_clr, ctx->hw.data_mask);
    }
}

static inline void sbw_data_release_now(sbw_ctx_t *ctx) {
    sbw_mmio_write(ctx->hw.gpio_oe_clr, ctx->hw.data_mask);
}

static inline bool sbw_data_read(const sbw_ctx_t *ctx) {
    return (sbw_mmio_read(ctx->hw.gpio_in) & ctx->hw.data_mask) != 0;
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

    sbw_mmio_write(ctx->hw.gpio_oe_set, ctx->hw.data_mask);
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

    sbw_mmio_write(ctx->hw.gpio_oe_set, ctx->hw.data_mask);
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
    volatile uint32_t *const gpio_in = ctx->hw.gpio_in;
    volatile uint32_t *const gpio_out_set = ctx->hw.gpio_out_set;
    volatile uint32_t *const gpio_out_clr = ctx->hw.gpio_out_clr;
    volatile uint32_t *const gpio_oe_clr = ctx->hw.gpio_oe_clr;
    volatile uint32_t *const gpio_oe_set = ctx->hw.gpio_oe_set;
    const uint32_t clock_mask = ctx->hw.clock_mask;
    const uint32_t data_mask = ctx->hw.data_mask;

    // One logical SBW bit is three clocked sub-slots on SBWTDIO: drive TMS, drive TDI, then
    // release the line so the target can return TDO on the third low pulse.
    // Active-session callers keep SBWTDIO driven between logical bits, so this sequence starts
    // with the master already owning the bus.
    // Slot 1: present TMS while we still own SBWTDIO.
    sbw_mmio_write(tms ? gpio_out_set : gpio_out_clr, data_mask);
    sbw_wait_cycles(SBW_ACTIVE_SLOT_HIGH_CYCLES);
    sbw_irq_disable();
    sbw_mmio_write(gpio_out_clr, clock_mask);
    sbw_wait_cycles(SBW_ACTIVE_SLOT_LOW_CYCLES);
    sbw_mmio_write(gpio_out_set, clock_mask);
    sbw_irq_enable();

    // Slot 2: present TDI. In Run-Test/Idle this slot also carries TCLK state.
    sbw_mmio_write(tdi ? gpio_out_set : gpio_out_clr, data_mask);
    sbw_wait_cycles(SBW_ACTIVE_SLOT_HIGH_CYCLES);
    sbw_irq_disable();
    sbw_mmio_write(gpio_out_clr, clock_mask);
    sbw_wait_cycles(SBW_ACTIVE_SLOT_LOW_CYCLES);
    sbw_mmio_write(gpio_out_set, clock_mask);
    sbw_irq_enable();

    // Slot 3: release SBWTDIO before the falling edge so the target can drive TDO.
    // Once SBWTCK returns high, the target should have released the line, so we can
    // immediately take ownership again instead of leaving the bus floating between bits.
    sbw_mmio_write(gpio_oe_clr, data_mask);
    sbw_wait_cycles(SBW_ACTIVE_SLOT_HIGH_CYCLES);
    sbw_irq_disable();
    sbw_mmio_write(gpio_out_clr, clock_mask);
    sbw_wait_cycles(SBW_ACTIVE_SLOT_LOW_CYCLES);
    const bool tdo = (sbw_mmio_read(gpio_in) & data_mask) != 0;
    sbw_mmio_write(gpio_out_set, clock_mask);
    sbw_mmio_write(gpio_oe_set, data_mask);
    sbw_irq_enable();
    return tdo;
}

static inline void sbw_tclk_set(sbw_ctx_t *ctx, bool high) {
    volatile uint32_t *const gpio_out_set = ctx->hw.gpio_out_set;
    volatile uint32_t *const gpio_out_clr = ctx->hw.gpio_out_clr;
    volatile uint32_t *const gpio_oe_clr = ctx->hw.gpio_oe_clr;
    volatile uint32_t *const gpio_oe_set = ctx->hw.gpio_oe_set;
    const uint32_t clock_mask = ctx->hw.clock_mask;
    const uint32_t data_mask = ctx->hw.data_mask;

    if (ctx->tclk_high) {
        sbw_mmio_write(gpio_out_clr, data_mask);
        sbw_wait_cycles(SBW_ACTIVE_SLOT_HIGH_CYCLES);
        sbw_irq_disable();
        sbw_mmio_write(gpio_out_clr, clock_mask);
        sbw_wait_cycles(SBW_TMSLDH_LOW_BEFORE_DRIVE_CYCLES);
        sbw_mmio_write(gpio_out_set, data_mask);
        sbw_wait_cycles(SBW_ACTIVE_SLOT_LOW_CYCLES - SBW_TMSLDH_LOW_BEFORE_DRIVE_CYCLES);
        sbw_mmio_write(gpio_out_set, clock_mask);
        sbw_irq_enable();
    } else {
        sbw_mmio_write(gpio_out_clr, data_mask);
        sbw_wait_cycles(SBW_ACTIVE_SLOT_HIGH_CYCLES);
        sbw_irq_disable();
        sbw_mmio_write(gpio_out_clr, clock_mask);
        sbw_wait_cycles(SBW_ACTIVE_SLOT_LOW_CYCLES);
        sbw_mmio_write(gpio_out_set, clock_mask);
        sbw_irq_enable();
    }

    sbw_mmio_write(high ? gpio_out_set : gpio_out_clr, data_mask);
    sbw_wait_cycles(SBW_ACTIVE_SLOT_HIGH_CYCLES);
    sbw_irq_disable();
    sbw_mmio_write(gpio_out_clr, clock_mask);
    sbw_wait_cycles(SBW_ACTIVE_SLOT_LOW_CYCLES);
    sbw_mmio_write(gpio_out_set, clock_mask);
    sbw_irq_enable();

    sbw_mmio_write(gpio_oe_clr, data_mask);
    sbw_wait_cycles(SBW_ACTIVE_SLOT_HIGH_CYCLES);
    sbw_irq_disable();
    sbw_mmio_write(gpio_out_clr, clock_mask);
    sbw_wait_cycles(SBW_ACTIVE_SLOT_LOW_CYCLES);
    sbw_mmio_write(gpio_out_set, clock_mask);
    sbw_mmio_write(gpio_oe_set, data_mask);
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

static void sbw_shift_ir8_payload_finish_no_capture(sbw_ctx_t *ctx, uint8_t instruction) {
    uint8_t shift = instruction;
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

static uint32_t sbw_shift_ir8_capture_trace(sbw_ctx_t *ctx, uint8_t instruction, uint8_t *captured_out, size_t *packet_count_out) {
    uint32_t trace = 0u;
    size_t packet_count = 0u;
    uint8_t captured = 0u;
    uint8_t shift = instruction;

    trace = (trace << 1u) | (sbw_io_bit(ctx, true, ctx->tclk_high) ? 1u : 0u);
    ++packet_count;
    trace = (trace << 1u) | (sbw_io_bit(ctx, true, true) ? 1u : 0u);
    ++packet_count;
    trace = (trace << 1u) | (sbw_io_bit(ctx, false, true) ? 1u : 0u);
    ++packet_count;
    trace = (trace << 1u) | (sbw_io_bit(ctx, false, true) ? 1u : 0u);
    ++packet_count;

    for (uint32_t bit = 0; bit < 7; ++bit) {
        const bool sampled = sbw_io_bit(ctx, false, (shift & 0x1u) != 0);
        trace = (trace << 1u) | (sampled ? 1u : 0u);
        captured = (uint8_t)((captured << 1u) | (sampled ? 1u : 0u));
        ++packet_count;
        shift >>= 1;
    }

    {
        const bool sampled = sbw_io_bit(ctx, true, (shift & 0x1u) != 0);
        trace = (trace << 1u) | (sampled ? 1u : 0u);
        captured = (uint8_t)((captured << 1u) | (sampled ? 1u : 0u));
        ++packet_count;
    }

    trace = (trace << 1u) | (sbw_io_bit(ctx, true, true) ? 1u : 0u);
    ++packet_count;
    trace = (trace << 1u) | (sbw_io_bit(ctx, false, ctx->tclk_high) ? 1u : 0u);
    ++packet_count;

    if (captured_out) {
        *captured_out = captured;
    }
    if (packet_count_out) {
        *packet_count_out = packet_count;
    }
    return trace;
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
    sbw_shift_dr16_no_capture(ctx, SBW_SAFE_FRAM_PC);

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

    // Public contract: a block write must stay within a single protection class
    // (RAM/peripheral, info FRAM, or main FRAM).
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

    const bool write_ok = sbw_pio_stream_write_block16_quick(ctx, data, word_count) &&
        sbw_finish_write_block16_quick_430xv2(ctx);
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
    sbw_hw_desc_t hw;
    sbw_ctx_t ctx;
    uint8_t last_id = 0;
    bool ok = false;

    sbw_parse_hw_desc(hw_in, &hw);
    sbw_ctx_init(&ctx, &hw);

    for (uint32_t attempt = 0; attempt < SBW_JTAG_ATTEMPTS; ++attempt) {
        sbw_begin_session(&ctx);
        ok = sbw_shift_ir8_capture_pio(&ctx, SBW_IR_CNTRL_SIG_CAPTURE, &last_id, NULL, 0u, NULL, NULL, NULL);
        sbw_release(&ctx);
        if (ok && last_id == SBW_JTAG_ID_EXPECTED) {
            break;
        }
    }

    return sbw_make_bool_u8(ok && last_id == SBW_JTAG_ID_EXPECTED, last_id);
}
static MP_DEFINE_CONST_FUN_OBJ_1(sbw_native_read_id_obj, sbw_native_read_id);

static mp_obj_t sbw_native_pio_read_id_debug(mp_obj_t hw_in) {
    sbw_hw_desc_t hw;
    sbw_ctx_t ctx;
    uint8_t last_id = 0;
    uint32_t rx_words[2] = {0u, 0u};
    size_t rx_word_count = 0;
    size_t packet_count = 0;
    uint32_t trace = 0u;
    bool ok = false;

    sbw_parse_hw_desc(hw_in, &hw);
    sbw_ctx_init(&ctx, &hw);

    sbw_begin_session(&ctx);
    ok = sbw_shift_ir8_capture_pio(&ctx, SBW_IR_CNTRL_SIG_CAPTURE, &last_id,
        rx_words, 2u, &rx_word_count, &packet_count, &trace);
    sbw_release(&ctx);

    mp_obj_t items[5] = {
        mp_obj_new_bool(ok),
        mp_obj_new_int_from_uint(last_id),
        mp_obj_new_int_from_uint(trace),
        mp_obj_new_int_from_uint((mp_uint_t)packet_count),
        mp_obj_new_bytes((const uint8_t *)rx_words, rx_word_count * sizeof(uint32_t)),
    };
    return mp_obj_new_tuple(5, items);
}
static MP_DEFINE_CONST_FUN_OBJ_1(sbw_native_pio_read_id_debug_obj, sbw_native_pio_read_id_debug);

static mp_obj_t sbw_native_bitbang_read_id_debug(mp_obj_t hw_in) {
    sbw_hw_desc_t hw;
    sbw_ctx_t ctx;
    uint8_t last_id = 0;
    size_t packet_count = 0;
    uint32_t trace = 0u;

    sbw_parse_hw_desc(hw_in, &hw);
    sbw_ctx_init(&ctx, &hw);

    sbw_begin_session(&ctx);
    trace = sbw_shift_ir8_capture_trace(&ctx, SBW_IR_CNTRL_SIG_CAPTURE, &last_id, &packet_count);
    sbw_release(&ctx);

    mp_obj_t items[4] = {
        mp_obj_new_bool(last_id == SBW_JTAG_ID_EXPECTED),
        mp_obj_new_int_from_uint(last_id),
        mp_obj_new_int_from_uint(trace),
        mp_obj_new_int_from_uint((mp_uint_t)packet_count),
    };
    return mp_obj_new_tuple(4, items);
}
static MP_DEFINE_CONST_FUN_OBJ_1(sbw_native_bitbang_read_id_debug_obj, sbw_native_bitbang_read_id_debug);

static mp_obj_t sbw_native_pio_bypass_bitbang_debug(mp_obj_t hw_in) {
    sbw_hw_desc_t hw;
    sbw_ctx_t ctx;
    uint16_t full_pio = 0u;
    uint16_t go_ir_only = 0u;
    uint16_t tail_only = 0u;
    uint16_t handoff_only = 0u;
    bool ok_full = false;
    bool ok_go_ir = false;
    bool ok_tail = false;
    bool ok_handoff = false;

    sbw_parse_hw_desc(hw_in, &hw);
    sbw_ctx_init(&ctx, &hw);

    sbw_begin_session(&ctx);
    ok_full = sbw_shift_ir8_no_capture_pio(&ctx, SBW_IR_BYPASS);
    if (ok_full) {
        full_pio = sbw_shift_dr16_capture(&ctx, SBW_BYPASS_SMOKE_PATTERN);
    }
    sbw_release(&ctx);

    sbw_begin_session(&ctx);
    ok_go_ir = sbw_go_to_shift_ir_pio(&ctx);
    if (ok_go_ir) {
        sbw_shift_ir8_payload_finish_no_capture(&ctx, SBW_IR_BYPASS);
        go_ir_only = sbw_shift_dr16_capture(&ctx, SBW_BYPASS_SMOKE_PATTERN);
    }
    sbw_release(&ctx);

    sbw_begin_session(&ctx);
    sbw_go_to_shift_ir(&ctx);
    ok_tail = sbw_shift_ir8_payload_finish_pio(&ctx, SBW_IR_BYPASS);
    if (ok_tail) {
        tail_only = sbw_shift_dr16_capture(&ctx, SBW_BYPASS_SMOKE_PATTERN);
    }
    sbw_release(&ctx);

    sbw_begin_session(&ctx);
    sbw_pio_take_packet_pins(&ctx);
    sbw_pio_release_pins(&ctx);
    sbw_shift_ir8_no_capture(&ctx, SBW_IR_BYPASS);
    handoff_only = sbw_shift_dr16_capture(&ctx, SBW_BYPASS_SMOKE_PATTERN);
    ok_handoff = true;
    sbw_release(&ctx);

    mp_obj_t items[8] = {
        mp_obj_new_bool(ok_full),
        mp_obj_new_int_from_uint(full_pio),
        mp_obj_new_bool(ok_go_ir),
        mp_obj_new_int_from_uint(go_ir_only),
        mp_obj_new_bool(ok_tail),
        mp_obj_new_int_from_uint(tail_only),
        mp_obj_new_bool(ok_handoff),
        mp_obj_new_int_from_uint(handoff_only),
    };
    return mp_obj_new_tuple(8, items);
}
static MP_DEFINE_CONST_FUN_OBJ_1(sbw_native_pio_bypass_bitbang_debug_obj, sbw_native_pio_bypass_bitbang_debug);

static mp_obj_t sbw_native_bypass_test(mp_obj_t hw_in) {
    sbw_hw_desc_t hw;
    sbw_ctx_t ctx;
    uint16_t captured = 0;
    bool ok = false;

    sbw_parse_hw_desc(hw_in, &hw);
    sbw_ctx_init(&ctx, &hw);

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
    sbw_hw_desc_t hw;
    sbw_ctx_t ctx;
    uint16_t capture = 0;
    bool ok = false;

    sbw_parse_hw_desc(hw_in, &hw);
    sbw_ctx_init(&ctx, &hw);

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
    sbw_hw_desc_t hw;
    sbw_ctx_t ctx;
    const uint32_t address = (uint32_t)mp_obj_get_int_truncated(address_in);
    uint16_t data = 0;
    bool ok = false;

    sbw_parse_hw_desc(hw_in, &hw);
    sbw_ctx_init(&ctx, &hw);

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
    sbw_hw_desc_t hw;
    sbw_ctx_t ctx;
    const uint32_t address = (uint32_t)mp_obj_get_int_truncated(address_in);
    const size_t word_count = (size_t)mp_obj_get_int_truncated(words_in);
    uint16_t *buffer = NULL;
    bool ok = false;

    sbw_parse_hw_desc(hw_in, &hw);
    sbw_ctx_init(&ctx, &hw);

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
    sbw_hw_desc_t hw;
    sbw_ctx_t ctx;
    const uint32_t address = (uint32_t)mp_obj_get_int_truncated(address_in);
    const uint16_t value = (uint16_t)mp_obj_get_int_truncated(value_in);
    uint16_t readback = 0;
    bool ok = false;

    sbw_parse_hw_desc(hw_in, &hw);
    sbw_ctx_init(&ctx, &hw);

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
    sbw_hw_desc_t hw;
    sbw_ctx_t ctx;
    mp_buffer_info_t bufinfo;
    const uint32_t address = (uint32_t)mp_obj_get_int_truncated(address_in);

    sbw_parse_hw_desc(hw_in, &hw);
    sbw_ctx_init(&ctx, &hw);
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

static mp_obj_t sbw_native_pio_test_word(mp_obj_t hw_in, mp_obj_t pattern_word_in, mp_obj_t fifo_words_in) {
    sbw_hw_desc_t hw;
    sbw_ctx_t ctx;
    const uint32_t pattern_word = (uint32_t)mp_obj_get_int_truncated(pattern_word_in);
    const size_t fifo_word_count = (size_t)mp_obj_get_int_truncated(fifo_words_in);

    sbw_parse_hw_desc(hw_in, &hw);
    sbw_ctx_init(&ctx, &hw);

    return mp_obj_new_bool(sbw_pio_stream_test_pattern_word(&ctx, pattern_word, fifo_word_count));
}
static MP_DEFINE_CONST_FUN_OBJ_3(sbw_native_pio_test_word_obj, sbw_native_pio_test_word);

static mp_obj_t sbw_native_pio_packet_words(mp_obj_t hw_in, mp_obj_t data_in) {
    sbw_hw_desc_t hw;
    sbw_ctx_t ctx;
    mp_buffer_info_t bufinfo;

    sbw_parse_hw_desc(hw_in, &hw);
    sbw_ctx_init(&ctx, &hw);
    mp_get_buffer_raise(data_in, &bufinfo, MP_BUFFER_READ);
    uint32_t *words = NULL;
    size_t word_count = 0;
    size_t packet_count = 0;

    if (!sbw_parse_packet_words(&bufinfo, &words, &word_count, &packet_count)) {
        mp_raise_ValueError("packet data must be a sequence of 32-bit packet words");
    }

    const bool ok = sbw_pio_stream_packet_words(&ctx, words, word_count, packet_count);
    if (words) {
        m_free(words);
    }

    return mp_obj_new_bool(ok);
}
static MP_DEFINE_CONST_FUN_OBJ_2(sbw_native_pio_packet_words_obj, sbw_native_pio_packet_words);

static mp_obj_t sbw_native_pio_packet_words_capture(mp_obj_t hw_in, mp_obj_t data_in) {
    sbw_hw_desc_t hw;
    sbw_ctx_t ctx;
    mp_buffer_info_t bufinfo;
    uint32_t *words = NULL;
    size_t word_count = 0;
    size_t packet_count = 0;

    sbw_parse_hw_desc(hw_in, &hw);
    sbw_ctx_init(&ctx, &hw);
    mp_get_buffer_raise(data_in, &bufinfo, MP_BUFFER_READ);

    if (!sbw_parse_packet_words(&bufinfo, &words, &word_count, &packet_count)) {
        mp_raise_ValueError("packet data must be a sequence of 32-bit packet words");
    }

    const size_t rx_capacity = word_count;
    uint32_t *rx_words = NULL;
    size_t rx_word_count = 0;
    bool ok = false;

    if (rx_capacity != 0u) {
        rx_words = (uint32_t *)m_malloc(rx_capacity * sizeof(uint32_t));
    }

    ok = sbw_pio_run_packet_words(&ctx, words, word_count, packet_count, rx_words, rx_capacity, &rx_word_count);
    mp_obj_t result = sbw_make_bool_bytes(ok, (const uint8_t *)rx_words, rx_word_count * sizeof(uint32_t));

    if (rx_words) {
        m_free(rx_words);
    }
    if (words) {
        m_free(words);
    }
    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_2(sbw_native_pio_packet_words_capture_obj, sbw_native_pio_packet_words_capture);

static mp_obj_t sbw_native_pio_clock_square(mp_obj_t hw_in, mp_obj_t duration_us_in) {
    sbw_hw_desc_t hw;
    sbw_ctx_t ctx;
    const uint32_t duration_us = (uint32_t)mp_obj_get_int_truncated(duration_us_in);

    sbw_parse_hw_desc(hw_in, &hw);
    sbw_ctx_init(&ctx, &hw);

    return mp_obj_new_bool(sbw_pio_clock_square_wave_us(&ctx, duration_us));
}
static MP_DEFINE_CONST_FUN_OBJ_2(sbw_native_pio_clock_square_obj, sbw_native_pio_clock_square);

static mp_obj_t sbw_native_pio_clock_data_square(mp_obj_t hw_in, mp_obj_t duration_us_in) {
    sbw_hw_desc_t hw;
    sbw_ctx_t ctx;
    const uint32_t duration_us = (uint32_t)mp_obj_get_int_truncated(duration_us_in);

    sbw_parse_hw_desc(hw_in, &hw);
    sbw_ctx_init(&ctx, &hw);

    return mp_obj_new_bool(sbw_pio_clock_data_square_wave_us(&ctx, duration_us));
}
static MP_DEFINE_CONST_FUN_OBJ_2(sbw_native_pio_clock_data_square_obj, sbw_native_pio_clock_data_square);

mp_obj_t mpy_init(mp_obj_fun_bc_t *self, size_t n_args, size_t n_kw, mp_obj_t *args) {
    MP_DYNRUNTIME_INIT_ENTRY

    mp_store_global(MP_QSTR_read_id, MP_OBJ_FROM_PTR(&sbw_native_read_id_obj));
    mp_store_global(MP_QSTR_pio_read_id_debug, MP_OBJ_FROM_PTR(&sbw_native_pio_read_id_debug_obj));
    mp_store_global(MP_QSTR_pio_id_dbg, MP_OBJ_FROM_PTR(&sbw_native_pio_read_id_debug_obj));
    mp_store_global(MP_QSTR_bitbang_read_id_debug, MP_OBJ_FROM_PTR(&sbw_native_bitbang_read_id_debug_obj));
    mp_store_global(MP_QSTR_bitbang_id_dbg, MP_OBJ_FROM_PTR(&sbw_native_bitbang_read_id_debug_obj));
    mp_store_global(MP_QSTR_pio_bypass_bitbang_debug, MP_OBJ_FROM_PTR(&sbw_native_pio_bypass_bitbang_debug_obj));
    mp_store_global(MP_QSTR_pio_bypass_dbg, MP_OBJ_FROM_PTR(&sbw_native_pio_bypass_bitbang_debug_obj));
    mp_store_global(MP_QSTR_bypass_test, MP_OBJ_FROM_PTR(&sbw_native_bypass_test_obj));
    mp_store_global(MP_QSTR_sync_and_por, MP_OBJ_FROM_PTR(&sbw_native_sync_and_por_obj));
    mp_store_global(MP_QSTR_read_mem16, MP_OBJ_FROM_PTR(&sbw_native_read_mem16_obj));
    mp_store_global(MP_QSTR_write_mem16, MP_OBJ_FROM_PTR(&sbw_native_write_mem16_obj));
    mp_store_global(MP_QSTR_read_block16, MP_OBJ_FROM_PTR(&sbw_native_read_block16_obj));
    mp_store_global(MP_QSTR_write_block16, MP_OBJ_FROM_PTR(&sbw_native_write_block16_obj));
    mp_store_global(MP_QSTR_pio_test_word, MP_OBJ_FROM_PTR(&sbw_native_pio_test_word_obj));
    mp_store_global(MP_QSTR_pio_packet_words, MP_OBJ_FROM_PTR(&sbw_native_pio_packet_words_obj));
    mp_store_global(MP_QSTR_pio_packet_words_capture, MP_OBJ_FROM_PTR(&sbw_native_pio_packet_words_capture_obj));
    mp_store_global(MP_QSTR_pio_clock_square, MP_OBJ_FROM_PTR(&sbw_native_pio_clock_square_obj));
    mp_store_global(MP_QSTR_pio_clock_data_square, MP_OBJ_FROM_PTR(&sbw_native_pio_clock_data_square_obj));
    mp_store_global(MP_QSTR_SYS_CLK_HZ, mp_obj_new_int_from_uint(SBW_SYS_CLK_HZ));

    MP_DYNRUNTIME_INIT_EXIT
}
