# Pico PIO SBW Handoff

## Goal

Build a robust MSP430FR4133 SBW programmer around a Raspberry Pi Pico (RP2040) using PIO for the timing-critical wire protocol.

The immediate bring-up goal should be:

1. Enter SBW mode.
2. Reset the JTAG TAP.
3. Read the MSP430 JTAG ID.
4. Enter BYPASS and verify known shift behavior.

Only after that should the implementation move on to MSP430-specific memory access and performance work.


## What We Learned From The Pi Attempt

### 1. The hard part is not general JTAG logic, it is the SBW bit cell timing

The higher-level JTAG sequencing is understood well enough to use as a reference:

- TAP reset is straightforward.
- A good JTAG ID read on this target returns `0x98`.
- A good BYPASS test with input `0xA55A` returns `0x52AD`.

Those values are useful smoke tests for the Pico version.

### 2. Linux userspace is the wrong place to sample TDO

Several Pi approaches were able to generate edges, but anything that depended on software noticing the TDO window and sampling inside it was fundamentally fragile.

The core failure mode was always the same:

- drive `SBWTCK` low
- somehow wait
- try to sample `SBWTDIO` during the TDO sub-slot

If the sample timing depends on Linux scheduling, MMIO polling, or syscall timing, the implementation is not trustworthy.

### 3. SPI/DMA can generate clocks, but MOSI idle behavior is a real trap

Useful findings:

- SPI-generated clocks are deterministic enough to be interesting.
- AUX SPI `SCLK` on the Pi was proven to generate clean, repeated clock trains.
- AUX SPI `MOSI` data was proven to toggle correctly during transfers.

But there was a fatal limitation for the "use MOSI as SBW clock" idea:

- `MOSI` idles low or otherwise uncontrolled between transfers.
- There is no clean "idle-high clock line" guarantee on MOSI.

That makes MOSI a poor fit for SBW clock generation.

### 4. A dedicated hardware-timed engine is the right abstraction boundary

The good abstraction is:

- host provides logical JTAG bits: `TMS`, `TDI`
- low-level engine returns logical `TDO`

The engine must hide the three SBW sub-slots internally:

1. `TMS` sub-slot
2. `TDI` sub-slot
3. `TDO` sub-slot

That abstraction should be preserved on the Pico.

### 5. Bring-up should be correctness-first, not throughput-first

The Pi work repeatedly showed that trying to optimize before transport correctness wastes time.

For the Pico version:

- first make one logical JTAG bit rock solid
- then batch bits
- then batch IR/DR shifts
- then optimize block transfers


## Why Pico PIO Is A Better Fit

PIO is a much better match for SBW because it can do all of these deterministically:

- drive `SBWTCK` low and high at fixed cycle counts
- switch `SBWTDIO` between output and input at exact instruction boundaries
- sample `SBWTDIO` at a precise point during the TDO sub-slot
- repeat that bit cell indefinitely without host timing involvement

This directly addresses the problem that broke the Pi userspace and hybrid designs.


## Recommended Pico Wiring

Use one dedicated clock pin and one dedicated bidirectional data pin:

- `PIO pin A` -> target `TEST / SBWTCK`
- `PIO pin B` -> target `RST/NMI/SBWTDIO`
- shared `GND`
- target externally powered at `3.3 V`

Do not start with resistor tricks or split data-drive/data-sense wiring. The Pico can own one true bidirectional `SBWTDIO` pin directly, which is simpler and closer to the protocol.


## Recommended PIO Transport Boundary

Keep the same logical boundary that proved useful on the Pi:

- `start()`
- `io_bit(tms, tdi) -> tdo`
- `release()`

Then layer JTAG helpers above that:

- `tap_reset()`
- `shift_ir8()`
- `shift_dr16()`
- later, MSP430-specific IR/DR helpers

This prevents the codebase from baking SBW details into the JTAG layer.


## Suggested PIO Design

### First transport shape

Encode one logical JTAG bit as one PIO transaction:

- host sends two bits: `TMS`, `TDI`
- PIO executes one full SBW bit cell
- PIO returns one bit: `TDO`

Suggested TX packing:

- bit `1` = `TMS`
- bit `0` = `TDI`

Suggested RX packing:

- bit `0` = `TDO`

That keeps the host side extremely simple during bring-up.

### What the PIO bit cell should do

For each logical JTAG bit:

1. Drive `SBWTDIO` as output with the `TMS` value.
2. Generate one `SBWTCK` low pulse and return high.
3. Drive `SBWTDIO` as output with the `TDI` value.
4. Generate one `SBWTCK` low pulse and return high.
5. Change `SBWTDIO` to input.
6. Generate the `TDO` low pulse.
7. Sample `SBWTDIO` at a fixed point while clock is low.
8. Return `SBWTCK` high.
9. Optionally restore `SBWTDIO` direction to output-high idle at the end of the cell.

The sample point should be well inside the low window, not on an edge.

### Initial timing target

Start conservatively.

Suggested first-pass target:

- `SBWTCK` low: about `1 us`
- `SBWTCK` high: about `1 us`
- sample near the middle of the TDO low phase

That is slow enough to observe easily and fast enough to stay away from the "held low too long" failure mode that complicated the Pi work.

Do not try to start anywhere near the device's top SBW frequency.


## Bring-Up Plan

### Phase 1: Raw pin proof

Before any MSP430 traffic:

- prove the Pico can drive `SBWTCK` as a repeated low/high waveform
- prove the Pico can switch `SBWTDIO` between output and input
- prove the Pico can sample the `SBWTDIO` pin while clock is low

Use a logic analyzer for this phase.

### Phase 2: One logical JTAG bit

Implement `io_bit(tms, tdi) -> tdo` and prove the three sub-slots look correct on the analyzer.

This is the single most important milestone.

### Phase 3: TAP reset

Use the transport to issue:

- six or more `TMS=1` logical JTAG bits
- then one `TMS=0` bit to land in `Run-Test/Idle`

No MSP430-specific behavior yet.

### Phase 4: JTAG ID smoke test

Implement the smallest useful JTAG macro layer:

- `tap_reset()`
- `shift_ir8()`

First smoke test:

- enter SBW mode
- reset TAP
- shift `IR_CNTRL_SIG_CAPTURE`
- verify returned value is `0x98`

This is the first real pass/fail milestone.

### Phase 5: BYPASS smoke test

Add:

- `shift_dr16()`

Test:

- move to BYPASS
- shift `0xA55A`
- expect `0x52AD`

This proves that:

- TDI is making it in
- TDO is making it out
- bit ordering is correct
- JTAG state transitions are correct

### Phase 6: MSP430 core access

Only after JTAG ID and BYPASS are solid:

- implement MSP430-specific IR constants
- implement control-signal capture and sync
- implement a minimal memory read
- implement a minimal memory write

Start with one word, not a block transfer.

### Phase 7: Small FRAM read/write

Use a sacrificial FRAM address and:

- read initial word
- write a known test word
- read it back
- restore if needed

Keep this tiny and observable.

### Phase 8: Block transfer and performance

Only after correctness is established:

- batch multiple logical JTAG bits per FIFO load
- batch IR/DR shifts
- optimize host/Pico communication
- move on to quick write / quick read style operations


## Recommended First CLI Tests

The new Pico toolchain should expose small, explicit tests in this order:

1. `clock-test`
2. `sbw-slot-test`
3. `read-jtagid`
4. `bypass-test`
5. `read-word`
6. `write-word`
7. `fram-smoke`

This avoids debugging everything at once.


## Things To Avoid Repeating

- Do not let host software decide the TDO sample instant.
- Do not use MOSI as the SBW clock if idle-high matters between transfers.
- Do not start with DMA, throughput tuning, or block programming.
- Do not assume a promising waveform is good until JTAG ID and BYPASS both pass.
- Do not collapse the SBW transport and JTAG layers together.


## Suggested Success Criteria For Each Stage

### Transport stage

- analyzer shows correct 3-sub-slot SBW bit cells
- no unexpected long low pulse on `SBWTCK`

### JTAG stage

- `read-jtagid` repeatedly returns `0x98`
- `bypass-test` repeatedly returns `0x52AD`

### Memory stage

- single-word FRAM write/read passes repeatedly

### Performance stage

- larger write/read/verify works before any optimization
- only then chase the `< 2 s` target for `10 kB`


## Bottom Line

The Pi attempt was still valuable. It showed:

- the useful abstraction boundary
- the right first smoke tests
- the known-good expected JTAG responses
- and, most importantly, that the transport must own all SBW timing internally

That is exactly what PIO is good at.
