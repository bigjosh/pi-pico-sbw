# PIO SBW Implementation Notes

## Purpose

This document explains the approach used to get a Raspberry Pi Pico 2W talking to an `MSP430FR4133` over `Spy-Bi-Wire` (`SBW`), and how to carry that work forward into a `PIO` implementation.

It is written as a technical reproduction guide. A reader should be able to:

1. rebuild the current working GPIO-based reference implementation from scratch
2. understand the exact `SBW` and `JTAG` sequencing that was proven on bench hardware
3. replace the timing-critical `io_bit()` transport with a `PIO` function without losing the validated higher-level behavior

The current repo has not switched to `PIO` yet. The validated transport is still software-driven GPIO. That is intentional. The GPIO transport is the reference model that made it possible to prove the protocol one layer at a time.


## Current Validated Milestone

The following have been verified on the live target:

- `read-jtagid` returns `0x98`
- `bypass-test` returns `0x52AD` for input `0xA55A`
- `sync-por` returns `0xC301`, satisfying the `0x0301` Full-Emulation-State mask
- `read-mem16 0xfffe` consistently returns `0xEDC8`
- `read-mem16 0xfffc` consistently returns `0xEE44`
- `write-read-mem16 0x2000 0x1234` verifies RAM write/readback
- `write-read-mem16 0x2002 0xA55A` verifies RAM write/readback

That means the project is already past waveform bring-up. `SBW` entry, TAP reset, JTAG control, `POR`, memory read, and RAM write/readback all work.


## Hardware And Wiring

Bench wiring used for the working reference:

- `GP2` -> target `TEST / SBWTCK`
- `GP3` -> target `RST/NMI / SBWTDIO`
- `GP4` -> target `VCC` during lab bring-up only
- Pico `GND` -> target `GND`

Important constraints:

- `GP4` is only a temporary power source for bring-up
- `SBWTDIO` currently has no series resistor
- firmware must leave `SBWTDIO` as input except during intentional drive windows


## Primary Sources

The implementation was driven from TI primary documentation:

- `docs/Programming over JTAG slau320aj.pdf`
- `docs/MSP430FR4xx and MSP430FR2xx family Users guide slau445i.pdf`
- `docs/MSP430FR413x Mixed-Signal Microcontrollers msp430fr4133.pdf`

The key facts that mattered most were:

- `SBW` entry timing for FR4xx devices
- `ResetTAP` behavior in `SBW` mode
- IR instructions are conceptually `LSB`-first
- DR words are shifted `MSB`-first
- `SyncJtag_AssertPor()` and `ExecutePOR_430Xv2()` sequences
- FR4133 memory map:
  - RAM: `0x2000` to `0x27FF`
  - main FRAM: `0xC400` to `0xFFFF`
  - info FRAM: `0x1800` to `0x19FF`
  - `WDTCTL`: `0x01CC`
  - `SYSCFG0`: `0x0160`


## Architecture

The working code is intentionally layered:

- `src/sbw_hw.c`
  Owns raw GPIO states and direction changes.
- `src/sbw_transport_gpio.c`
  Encodes the `SBW` time slots and entry sequences.
- `src/sbw_jtag.c`
  Builds `JTAG` and MSP430-specific flows on top of transport primitives.
- `src/main.c`
  Exposes bench commands over USB serial.

That split should be preserved when `PIO` replaces the GPIO transport. The `JTAG` layer should not learn about pin timing.


## The Mental Model That Actually Worked

The project became reliable once the transport was treated as the primitive, not the `JTAG` sequence.

One logical `JTAG` bit in `SBW` is:

1. one `TMS` slot
2. one `TDI` slot
3. one `TDO` slot

That is the core transport boundary:

```c
bool io_bit(bool tms, bool tdi) -> tdo;
```

Everything above that is ordinary state-machine work.


## Working GPIO Reference Transport

### 1. SBW Entry

Two entry modes were implemented because TI uses both:

- `RST_HIGH` entry
- `RST_LOW` entry

The important correction was to stop using an over-simplified "hold TEST high for 120 us" idea and instead match the TI FR4xx entry sequence much more closely.

For `RST_HIGH`, the working reference does:

1. hold `SBWTCK` low for `4 ms`
2. drive `SBWTDIO` high
3. drive `SBWTCK` high for `20 ms`
4. keep `SBWTDIO` high for `60 us`
5. pulse `SBWTCK` low for `1 us`
6. drive `SBWTCK` high for `60 us`
7. wait `5 ms`

That logic lives in `sbw_transport_start_mode()`.

### 2. Basic SBW Slots

The working software transport is built from three slot types:

- drive data while clock is high, then pulse clock low/high
- release data, pulse clock low, sample during the low phase, then return high
- a special `TMSLDH` slot used to preserve a previously-high `TCLK` state

The current code implements these ideas as:

- `sbw_transport_slot_drive()`
- `sbw_transport_slot_tdo()`
- `sbw_transport_slot_tmsldh()`

The current reference transport keeps the timing model intentionally small:

- one compile-time low-phase duration per slot
- one compile-time high-phase duration per slot
- no separate user-facing sample-delay parameter

Instead, both slot phases are held with `busy_wait_at_least_cycles()` delays using cycle counts derived at compile time from `SYS_CLK_HZ`. Interrupts are masked only for the low phase, and the `TDO` sample is taken at the end of that low window immediately before returning `SBWTCK` high.

The current validated active-session baseline is a fixed compile-time `100 ns` high and `100 ns` low per slot.

The current GPIO reference also uses direct `SIO` register writes for the hot path and avoids redundant `SBWTDIO` direction writes when the line is already owned by the Pico.

### 3. Saved TCLK State

This is the non-obvious part that mattered for memory access.

The MSP430 reference code preserves a saved `TCLK` state while moving between `Run-Test/Idle` and IR/DR scan states. That means the first and last logical bits around every shift are not hard-coded. They depend on whether `TCLK` is currently considered high or low.

The current code tracks that with:

```c
static bool g_tclk_high = true;
```

and exposes:

```c
void sbw_transport_tclk_set(bool high);
bool sbw_transport_tclk_is_high(void);
```

Without that, `read-jtagid` and `bypass-test` can pass, but `SyncJtag_AssertPor()` and memory access are not reliable.


## Working JTAG Layer

### 1. TAP Reset

The working `SBW` `ResetTAP` sequence is:

- six logical bits with `TMS=1`, `TDI=1`
- one logical bit with `TMS=0`, `TDI=1`

The earlier extra pulses were wrong for this target.

### 2. IR And DR Bit Ordering

The code uses:

- IR shift: `LSB` first
- DR shift: `MSB` first

That is why the current constants use the original TI IR values:

- `IR_CNTRL_SIG_16BIT = 0x13`
- `IR_CNTRL_SIG_CAPTURE = 0x14`
- `IR_DATA_16BIT = 0x41`
- `IR_DATA_CAPTURE = 0x42`
- `IR_ADDR_16BIT = 0x83`
- `IR_DATA_TO_ADDR = 0x85`
- `IR_BYPASS = 0xFF`

### 3. Session Start

The current working session bring-up is:

1. release lines
2. wait `15 ms`
3. apply `RST_HIGH` SBW entry
4. `ResetTAP`

That is wrapped by `sbw_jtag_begin_session()`.

### 4. JTAG ID

The minimal proven smoke test is:

1. start session
2. `ResetTAP`
3. `IR_SHIFT(IR_CNTRL_SIG_CAPTURE)`
4. expect returned ID `0x98`

### 5. BYPASS

The second proven smoke test is:

1. start session
2. `ResetTAP`
3. `IR_SHIFT(IR_BYPASS)`
4. `DR_SHIFT16(0xA55A)`
5. expect `0x52AD`

### 6. SyncJtag_AssertPor

The next milestone after smoke tests was the FR4xx `SyncJtag_AssertPor()` path:

1. `IR_SHIFT(IR_CNTRL_SIG_16BIT)`
2. `DR_SHIFT16(0x1501)`
3. `IR_SHIFT(IR_CNTRL_SIG_CAPTURE)`
4. repeat `DR_SHIFT16(0x0000)` until bit `0x0200` is set
5. execute the FRAM-safe `POR` sequence
6. verify `(cntrl_sig & 0x0301) == 0x0301`

The working observed result is `0xC301`, which satisfies the mask.

### 7. ExecutePOR_430Xv2

The reference implementation in this repo follows the TI flow through the point where the target re-enters Full-Emulation-State:

1. toggle `TCLK` low/high
2. write `0x0C01`, then `0x0401` to `IR_CNTRL_SIG_16BIT`
3. load safe PC `0x0004` using `IR_DATA_16BIT`
4. issue `IR_DATA_CAPTURE`
5. reassert `CPUSUSP` with `0x0501`
6. verify control capture mask `0x0301`

After that, the code disables the watchdog by writing `0x5A80` to `WDTCTL` at `0x01CC`.

### 8. Memory Read

The current `ReadMem_430Xv2` style read is:

1. confirm Full-Emulation-State
2. `ClrTCLK()`
3. `IR_SHIFT(IR_CNTRL_SIG_16BIT)`
4. `DR_SHIFT16(0x0501)` for word read
5. `IR_SHIFT(IR_ADDR_16BIT)`
6. `DR_SHIFT20(address)`
7. `IR_SHIFT(IR_DATA_TO_ADDR)`
8. `SetTCLK()`
9. `ClrTCLK()`
10. `DR_SHIFT16(0x0000)` to read the data
11. `SetTCLK()`, `ClrTCLK()`, `SetTCLK()` to restore init state

### 9. Memory Write

The current word write flow is:

1. confirm Full-Emulation-State
2. `ClrTCLK()`
3. `IR_SHIFT(IR_CNTRL_SIG_16BIT)`
4. `DR_SHIFT16(0x0500)` for word write
5. `IR_SHIFT(IR_ADDR_16BIT)`
6. `DR_SHIFT20(address)`
7. `SetTCLK()`
8. `IR_SHIFT(IR_DATA_TO_ADDR)`
9. `DR_SHIFT16(data)`
10. `ClrTCLK()`
11. `IR_SHIFT(IR_CNTRL_SIG_16BIT)`
12. `DR_SHIFT16(0x0501)`
13. `SetTCLK()`, `ClrTCLK()`, `SetTCLK()`

The shell exposes a verified write/readback command for RAM:

```text
write-read-mem16 0x2000 0x1234
write-read-mem16 0x2002 0xA55A
```


## How To Reproduce The Current Working State From Scratch

### Step 1: Create the Pico SDK skeleton

Create a normal Pico SDK app with:

- `stdio_usb`
- one executable target
- no PIO yet

### Step 2: Add the hardware layer

Implement:

- `sbw_hw_clock_drive(bool high)`
- `sbw_hw_data_drive(bool level)`
- `sbw_hw_data_release()`
- `sbw_hw_data_read()`
- `sbw_hw_target_power_set(bool enabled)`

Rules:

- `SBWTDIO` defaults to input
- `SBWTCK` idles low between sessions
- `GP4` returns to input when target power is off

### Step 3: Implement the transport layer

Implement:

- `sbw_transport_start_mode()`
- `sbw_transport_release()`
- `sbw_transport_io_bit()`
- `sbw_transport_tclk_set()`
- `sbw_transport_tclk_is_high()`

Do not skip the saved `TCLK` logic.

### Step 4: Implement the JTAG layer

Implement:

- `sbw_jtag_tap_reset()`
- `sbw_jtag_shift_ir8()`
- `sbw_jtag_shift_dr16()`
- `sbw_jtag_shift_dr20()`
- `sbw_jtag_sync_cpu()`
- `sbw_jtag_execute_por()`
- `sbw_jtag_read_mem16_internal()`
- `sbw_jtag_write_mem16_internal()`

### Step 5: Add a minimal shell

The current useful command order is:

1. `power-on`
2. `read-jtagid`
3. `bypass-test`
4. `sync-por`
5. `read-mem16 0xfffe`
6. `write-read-mem16 0x2000 0x1234`

### Step 6: Confirm expected values

If the implementation is correct, you should be able to reach at least:

- `0x98` from `read-jtagid`
- `0x52AD` from `bypass-test`
- `0xC301` from `sync-por`


## Why The First PIO Cutover Should Be Narrow

Do not move everything to `PIO` at once.

The proven approach is:

1. keep SBW entry in C at first
2. keep the current GPIO `TCLK` reference path until the `PIO` bit-cell is proven
3. move only `io_bit(tms, tdi) -> tdo` into `PIO`
4. re-run `read-jtagid`
5. re-run `bypass-test`
6. only then migrate the saved-`TCLK` helper if needed

This keeps the blast radius small.


## Proposed PIO Transport Contract

Use one state machine and one transaction per logical `JTAG` bit.

TX word:

- bit `0` = `TMS`
- bit `1` = `TDI`

RX word:

- bit `0` = `TDO`

Pin usage:

- `out_base` = `SBWTDIO`
- `set_base` = `SBWTDIO`
- `in_base` = `SBWTDIO`
- `sideset_base` = `SBWTCK`

The `PIO` program should only own the three-slot bit cell:

1. drive `TMS`
2. pulse clock low/high
3. drive `TDI`
4. pulse clock low/high
5. release data
6. sample `TDO` while clock is low
7. return clock high


## Proposed PIO Source For io_bit()

This is the first `PIO` program I would actually implement.

```pio
.program sbw_io_bit
.side_set 1

; TX format, shift-right:
;   bit 0 = TMS
;   bit 1 = TDI
;
; RX format:
;   bit 0 = TDO

.wrap_target
    pull block       side 1   ; clock idle high, get {tdi,tms}
    set pindirs, 1   side 1   ; own SBWTDIO

    out pins, 1      side 1   ; TMS slot
    nop              side 0 [3]
    nop              side 1 [3]

    out pins, 1      side 1   ; TDI slot
    nop              side 0 [3]
    nop              side 1 [3]

    set pindirs, 0   side 1   ; release for TDO
    nop              side 0 [1]
    in pins, 1       side 0 [1]
    nop              side 1 [3]

    push block       side 1
.wrap
```

What this does:

- two `out pins, 1` instructions consume `TMS` then `TDI`
- `set pindirs, 0` hands `SBWTDIO` back to the target
- `in pins, 1` samples during the low phase of the `TDO` slot
- the state machine returns with clock high

This is deliberately conservative. The exact delay counts should be tuned only after analyzer confirmation.


## Suggested PIO Host-Side Initialization

This is the minimum C-side setup needed to drive that program.

```c
PIO pio = pio0;
uint sm = 0;
uint offset = pio_add_program(pio, &sbw_io_bit_program);

pio_gpio_init(pio, SBW_PIN_CLOCK);
pio_gpio_init(pio, SBW_PIN_DATA);

pio_sm_config c = sbw_io_bit_program_get_default_config(offset);

sm_config_set_out_pins(&c, SBW_PIN_DATA, 1);
sm_config_set_set_pins(&c, SBW_PIN_DATA, 1);
sm_config_set_in_pins(&c, SBW_PIN_DATA);
sm_config_set_sideset_pins(&c, SBW_PIN_CLOCK);

sm_config_set_out_shift(&c, true, false, 32);
sm_config_set_in_shift(&c, true, false, 32);

sm_config_set_clkdiv(&c, 12.0f);

pio_sm_init(pio, sm, offset, &c);

pio_sm_set_consecutive_pindirs(pio, sm, SBW_PIN_CLOCK, 1, true);
pio_sm_set_pins_with_mask(
    pio,
    sm,
    1u << SBW_PIN_CLOCK,
    (1u << SBW_PIN_CLOCK) | (1u << SBW_PIN_DATA));

pio_sm_set_enabled(pio, sm, true);
```

And the host-side `io_bit()` wrapper:

```c
static bool sbw_pio_io_bit(PIO pio, uint sm, bool tms, bool tdi) {
    const uint32_t tx = (uint32_t)(tms ? 1u : 0u) |
        (uint32_t)((tdi ? 1u : 0u) << 1);

    pio_sm_put_blocking(pio, sm, tx);
    return (pio_sm_get_blocking(pio, sm) & 0x1u) != 0;
}
```

That is enough to replace the current software `io_bit()` for smoke testing.


## How To Validate The PIO Version

Validation order matters.

### Phase 1: Analyzer-only

Verify:

- clock idles high inside the bit cell
- data direction switches only during the TDO slot
- the sample point sits clearly inside the low phase

### Phase 2: Smoke tests

Run:

1. `read-jtagid`
2. `bypass-test`

Do not move on until they pass repeatedly.

### Phase 3: Re-enable the higher-level path

Once `io_bit()` is proven, reconnect the already-working:

- `ResetTAP`
- `sync-por`
- memory read
- RAM write/readback

The whole point of the layered design is that these should keep working once the transport is swapped.


## Recommended Next PIO Steps After io_bit()

After `sbw_io_bit` is validated:

1. decide whether to keep `sbw_transport_tclk_set()` in GPIO or move it into a second `PIO` primitive
2. add a batch mode that packs multiple logical bits into one FIFO burst
3. only then consider moving SBW entry into `PIO`

The first optimization target should be host-to-transport overhead, not raw clock rate.


## Common Failure Modes

### JTAG ID is `0x00` or BYPASS is `0xFFFF`

Usually means one of:

- entry sequence is wrong
- `SBWTDIO` direction handoff is late
- sample point is too close to an edge

### Smoke tests pass but Sync/POR fails

Usually means:

- saved `TCLK` state is not preserved
- first or last logical bit around IR/DR shifts is wrong

### Memory reads are unstable

Usually means:

- watchdog was not disabled after `POR`
- target is not really in Full-Emulation-State
- `TCLK` restore sequence after reads/writes is wrong


## Bottom Line

The correct path is:

1. preserve the current working layering
2. treat the GPIO transport as the truth model
3. move only `io_bit()` into `PIO` first
4. re-run the already-proven bench commands

The hard part was not discovering magical MSP430 behavior. The hard part was getting the transport exact enough that the ordinary TI `JTAG` flows started working. That is why the current repo should be treated as a protocol reference first and an optimization target second.
