# Status

## Snapshot

- Current repo direction: MicroPython orchestration + native C `.mpy` JTAG engine
- Older Pico SDK firmware path and abandoned `PIO` handoff material have been removed from the repo

## Live Bench State

The code path currently validated on hardware is the MicroPython path.

Validated environment:

- Board: `RPI_PICO2_W`
- Firmware: official MicroPython `v1.27.0`
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
- `testsuite.run_bench()` -> `(True, (5120, ~112000, ~114000, ~112000, ~114000))`
- regression rechecks single-word reads on the known RAM scratch words at `0x2000` / `0x2002`, and exercises the device descriptor window with `read_block16(0x1A00, 11)` expecting `0x0606` at `0x1A00` and `0x0811` at `0x1A14`
- The Python-side round-trip benchmark writes a generated `10,240` byte buffer, reads it back and verifies it, inverts every bit, then repeats the write/read/verify cycle
- `program.program_once(...)` completed successfully on the live target and returned device UUID `F081201041FBE32363002200`
- Approximate throughput:
  - write: `~90 KiB/s`
  - read: `~88 KiB/s`

## Current File Map

### Root

- [readme.MD](readme.MD)
  Top-level overview of the current architecture, hardware, build flow, and bench state.
- [status.md](status.md)
  This status snapshot and file map.
- [.gitignore](.gitignore)
  Ignore rules for local build outputs, caches, and temporary artifacts.

### Root App

- [main.py](main.py)
  Boot entrypoint for the production TSL programming loop.
- [program.py](program.py)
  Production TSL programmer. Loads `tsl-calibre-msp.txt`, writes the commissioning timestamp and firmware blocks separately, verifies each block by readback, then power-cycles the target to run it for 1 second.
- [debug_shell.py](debug_shell.py)
  Preserved bench shell for low-level SBW/JTAG bring-up and diagnostics.
- [sbw.py](sbw.py)
  Thin Python wrapper around the native module. Handles pin setup, target power switching, method forwarding into native C, the Python-side `mem_smoke16` helper, and byte-level read/write adapters built on the word-oriented native API.
- [sbw_config.py](sbw_config.py)
  Hardware descriptor tuple, pin assignments, regression constants, and the `150 MHz` clock setup/verification helper. SIO register addresses are defined here for Python-side power control; the native module hardcodes its own SIO base.
- [testsuite.py](testsuite.py)
  Automated regression and Python-side benchmark entry points for the live MicroPython path.
- [tsl-calibre-msp.txt](tsl-calibre-msp.txt)
  TSL production firmware image in TI-TXT format.

### `mpy/`

- [mpy/README.md](mpy/README.md)
  Native-module build/deploy notes.
- [mpy/SBW_NATIVE_API.md](mpy/SBW_NATIVE_API.md)
  Public API reference for the native `sbw_native` module.
- [mpy/sbw_native.c](mpy/sbw_native.c)
  The main native implementation file. Contains the timing-critical SBW transport, JTAG sequencing, and general target memory read/write logic. SIO base address (`0xD0000000`) is hardcoded; GPIO pin masks are passed from Python.
- [mpy/Makefile](mpy/Makefile)
  Build rules for generating `sbw_native.mpy` using MicroPython `dynruntime.mk`.

### `tools/`

- [tools/build-mpy-native.ps1](tools/build-mpy-native.ps1)
  Windows wrapper that invokes the native-module build through MSYS bash with the ARM embedded toolchain.

### `docs/`

- `docs/Programming over JTAG slau320aj.pdf`
  TI `JTAG`/`SBW` programming reference used as the main protocol source.
- `docs/MSP430FR4xx and MSP430FR2xx family Users guide slau445i.pdf`
  TI family user guide used for control-signal and FRAM behavior.
- `docs/MSP430FR413x Mixed-Signal Microcontrollers msp430fr4133.pdf`
  Target datasheet.

## Current Priorities

- keep the native C path correct and self-contained
- keep Python thin and policy-focused
- preserve the `150 MHz` clock contract explicitly
- optimize inside `mpy/sbw_native.c` if more throughput is needed

## Known Caveats

- The repo currently focuses on a single target family and is not a general-purpose MSP430 programmer.
