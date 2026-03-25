# Status

## Snapshot

- Branch: `codex/python-base-c-jtag`
- Last committed base: `9980665`
- Current repo direction: MicroPython orchestration + native C `.mpy` JTAG engine
- Older Pico SDK firmware path and abandoned `PIO` handoff material have been removed from the repo

## Live Bench State

The code path currently validated on hardware is the MicroPython path.

Validated environment:

- Board: `RPI_PICO2_W`
- Firmware: official MicroPython `v1.27.0`
- Host serial port during last test: `COM11`
- Target: `MSP430FR4133`
- Wiring:
  - `GP2` -> `TEST / SBWTCK`
  - `GP3` -> `/RST/NMI / SBWTDIO`
  - `GP4` -> target `VCC`
  - shared `GND`

Important runtime requirement:

- the native module is compiled for a maximum `150 MHz` system clock
- actual `clk_sys` must stay `<= SYS_CLK_HZ` or the native delay loops may violate `SBW` timing minimums
- the current `SBWNative()` wrapper enforces this conservatively by attempting `machine.freq(150000000)` and checking the result

Latest verified results:

- `testsuite.run_regression()` -> `True`
- `testsuite.run_bench()` -> `(True, (5120, 135506, 136416, 135392, 136275))`
- regression now explicitly exercises `read_block16(0xFFFC, 2)` and expects `0xEE44, 0xEDC8`
- The Python-side round-trip benchmark writes a generated `10,240` byte buffer, reads it back and verifies it, inverts every bit, then repeats the write/read/verify cycle
- Approximate throughput:
  - write pass 1: `74 KiB/s`
  - read pass 1: `73 KiB/s`
  - write pass 2: `74 KiB/s`
  - read pass 2: `73 KiB/s`

## Current File Map

### Root

- [readme.MD](D:/Github/pi-pico-sbw/readme.MD)
  Top-level overview of the current architecture, hardware, build flow, and bench state.
- [status.md](D:/Github/pi-pico-sbw/status.md)
  This status snapshot and file map.
- [.gitignore](D:/Github/pi-pico-sbw/.gitignore)
  Ignore rules for local build outputs, caches, and temporary artifacts.

### `mpy/`

- [mpy/main.py](D:/Github/pi-pico-sbw/mpy/main.py)
  Interactive MicroPython shell. Exposes power control and high-level commands like `read-jtagid`, `bypass-test`, `sync-por`, `read-mem16`, `fram-smoke16`, and the Python-side `fram-bench`.
- [mpy/sbw.py](D:/Github/pi-pico-sbw/mpy/sbw.py)
  Thin Python wrapper around the native module. Handles pin setup, target power switching, small formatting helpers, method forwarding into native C, and the Python-side `fram_smoke16` helper.
- [mpy/sbw_config.py](D:/Github/pi-pico-sbw/mpy/sbw_config.py)
  Hardware descriptor tuple, `RP2350` `SIO` MMIO addresses, pin assignments, regression constants, and the `150 MHz` clock setup/verification helper.
- [mpy/testsuite.py](D:/Github/pi-pico-sbw/mpy/testsuite.py)
  Automated regression and Python-side benchmark entry points for the live MicroPython path.
- [mpy/README.md](D:/Github/pi-pico-sbw/mpy/README.md)
  Detailed build/deploy notes for the MicroPython path.

### `mpy/native/`

- [mpy/native/sbw_native.c](D:/Github/pi-pico-sbw/mpy/native/sbw_native.c)
  The main implementation file. Contains:
  - direct MMIO `SIO` GPIO access
  - fixed cycle-counted `SBW` slot timing
  - interrupt masking around critical low pulses
  - `SBW` entry/release
  - `JTAG` TAP reset and IR/DR shifting
  - `SyncJtag_AssertPor` / `POR` flow
  - target memory read/write
  It intentionally does not contain benchmark timing, smoke-test policy, or synthetic-pattern policy anymore.
- [mpy/native/Makefile](D:/Github/pi-pico-sbw/mpy/native/Makefile)
  Build rules for generating `sbw_native.mpy` using MicroPython `dynruntime.mk`.

### `tools/`

- [tools/build-mpy-native.ps1](D:/Github/pi-pico-sbw/tools/build-mpy-native.ps1)
  Windows wrapper that invokes the native-module build through MSYS bash with the ARM embedded toolchain.

### `docs/`

- [docs/Programming over JTAG slau320aj.pdf](D:/Github/pi-pico-sbw/docs/Programming%20over%20JTAG%20slau320aj.pdf)
  TI `JTAG`/`SBW` programming reference used as the main protocol source.
- [docs/MSP430FR4xx and MSP430FR2xx family Users guide slau445i.pdf](D:/Github/pi-pico-sbw/docs/MSP430FR4xx%20and%20MSP430FR2xx%20family%20Users%20guide%20slau445i.pdf)
  TI family user guide used for control-signal and FRAM behavior.
- [docs/MSP430FR413x Mixed-Signal Microcontrollers msp430fr4133.pdf](D:/Github/pi-pico-sbw/docs/MSP430FR413x%20Mixed-Signal%20Microcontrollers%20msp430fr4133.pdf)
  Target datasheet.

## Current Priorities

- keep the native C path correct and self-contained
- keep Python thin and policy-focused
- preserve the `150 MHz` clock contract explicitly
- optimize inside `mpy/native/sbw_native.c` if more throughput is needed

## Known Caveats

- The working MicroPython path is slower than the earlier dedicated C firmware measurements preserved in git history.
- `sbw_config.py` must stay aligned with the actual MCU register map; an `RP2040`/`RP2350` offset mismatch breaks the transport completely.
- The repo currently focuses on a single target family and is not a general-purpose MSP430 programmer.
