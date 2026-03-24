# Raspberry Pi Pico SBW Programmer PoC

This directory contains a narrow, working proof of concept for talking to an `MSP430FR4133` over `SBW` from Raspberry Pi hardware.

The scope is intentionally small:

- open a connection
- read memory blocks
- write memory blocks
- close and release the target

Everything here is specialized for the actual target and workflow we have been testing. It is not a general-purpose MSP430 programmer.

## Why This Exists

The original production path used TI's MSPDS stack through `MSP430Flasher`. That worked, but the write path was still much slower than expected for a small FRAM device. The goal of this project is to explore how close a custom programmer can get to the real `SBW` throughput limit on `MSP430FR4133`.

This folder is the Raspberry Pi Pico side of that work.

## Primary Sources Used

The current `SBW` and target-side behavior is based on TI primary documentation, not on reverse-engineering MSPDS behavior:

- `MSP430 Programming With the JTAG Interface` (`SLAU320`)
- `MSP430FR4xx and MSP430FR2xx Family User's Guide` (`SLAU445`)
- `MSP430FR4133` datasheet

These documents are in the `docs` folder.

We will avoid using TI source code as a reference since it has proven buggy and low quality. Instead we will work from the primary sources above whenever possible and build up our own approaches to each step of this challenge.

## Hardware Setup

Target:

- `MSP430FR4133`
- `SBW`
- externally powered target or temporary `GP4`-powered bring-up

Raspberry Pi tested during development:

- Raspberry Pi Pico 2W

### Wiring

That maps to the physical header as:

- `GP3` -> target `/RST/NMI/SBWTDIO`
- `GP2` -> target `TEST/SBWTCK`
- Pico `GND` -> target `GND`
- `GP4` -> target `VCC` for lab bring-up power switching

These choices keep the two SBW pins adjacent and leave `GP0` and `GP1` free for UART debugging if needed.

### Power

The target can be powered from `GP4` during bring-up, but that should be treated as a lab-only convenience path.

For this FR4133 work:

- `VCC` switching is not required to reach the needed debug/programming states, but it is available for controlled bring-up tests
- only `RST` and `TEST` are actively driven by the Pi
- ground must be shared between Pi and target

Important constraints for the current bench setup:

- `GP4` is a GPIO used as a temporary target power source, not a robust power rail
- `SBWTDIO` has no series resistor right now, so firmware must leave it as input except during intentional drive windows
- target current draw must stay safely within what the Pico GPIO can source for this temporary setup

## Smoke Test

- enter `SBW`
- take the target under `JTAG` control
- read and verify JTAG ID

## Functionality test

- enter `SBW`
- take the target under `JTAG` control
- perform the documented FRAM-safe `POR` sequence
- disable the watchdog and clear FRAM write protection
- write a small word pattern
- read it back
- release the target

## Current Firmware Scaffold

The project now includes a minimal Pico SDK scaffold aimed at safe hardware bring-up before any `PIO` work:

- `src/sbw_hw.c` owns the physical pins and keeps `GP3` high-impedance unless explicitly driven
- `src/sbw_transport_gpio.c` implements a conservative software `io_bit()` shape plus the documented SBW entry and release sequences
- the current GPIO transport uses only compile-time timing constants, with both slot phases compiled to fixed cycle counts from `SYS_CLK_HZ`
- the active-session slot timing is fixed at `50 ns` low with no explicit added high delay; the effective high time comes from inherent software overhead, and interrupts are masked only around each short active-session low pulse
- the current GPIO hot path uses direct `SIO` register writes and only changes `SBWTDIO` direction when ownership actually changes
- the hot transport/JTAG files are built with LTO; the USB shell file is left out because full-target LTO conflicts with the current Pico stdio wrapper path
- `src/sbw_jtag.c` layers a minimal JTAG smoke-test path on top of the transport
- `src/main.c` exposes a small USB serial shell for early tests and analyzer-assisted bring-up

## Current Status

Host setup, build, flash, USB shell, and `GP4`-based target power switching are working.

The current firmware can:

- power the target on and off from the shell
- generate raw `SBWTCK` clocks for analyzer work
- generate one software SBW bit cell with explicit data direction handoff
- perform SBW entry, TAP reset, JTAG ID read, and BYPASS smoke test on bench hardware
- execute the FR4xx `SyncJtag_AssertPor()` path and confirm Full-Emulation-State
- read 16-bit target memory words
- write and verify 16-bit target RAM words
- clear and restore FR4xx FRAM protection in the write path when the target address is in FRAM
- expose a reversible `fram-smoke16` command for the first cautious FRAM proof
- expose `quick-read16` for small-block quick-read diagnostics
- expose `fram-bench` for destructive bulk FRAM write/verify timing

Current bench verification on the wired `MSP430FR4133`:

- `read-jtagid` returns `0x98`
- `bypass-test` returns `0x52AD` for the `0xA55A` smoke pattern
- `sync-por` returns `0xC301`, which satisfies the `0x0301` Full-Emulation-State mask
- `read-mem16 0xfffe` consistently returns `0xEDC8`
- `read-mem16 0xfffc` consistently returns `0xEE44`
- `write-read-mem16 0x2000 0x1234` verified RAM write/readback
- `write-read-mem16 0x2002 0xA55A` verified RAM write/readback
- `fram-smoke16 0xC400 0x1234` verified write/read/restore on live FRAM
- `fram-smoke16 0xC402 0xA55A` verified write/read/restore on live FRAM
- `quick-read16 0xC400 4` matched four known FRAM words written with the standard path
- `fram-bench 0xC400 5120` wrote and verified `10,240` bytes in `492,456 us`
- the smoke test passed repeatedly across multiple target power cycles

Current functional regression sequence:

- `power-on`
- `read-jtagid`
- `bypass-test`
- `sync-por`
- `read-mem16 0xfffe`
- `read-mem16 0xfffc`
- `write-read-mem16 0x2000 0x1234`
- `write-read-mem16 0x2002 0xA55A`
- `power-off`

This repo is now past raw bring-up and has a validated SBW/JTAG control baseline with working RAM and FRAM access. The current bulk-write path uses a single SBW/JTAG session with one FRAM unlock, repeated doc-based `WriteMem_430Xv2` word writes, and quick-read verification.

On the current wired target, `fram-bench 0xC400 5120` verifies a `10 KB` FRAM transfer in about `0.49 s`, which is roughly `20 KiB/s` write throughput and `87 KiB/s` quick-read verify throughput.

Current shell commands:

- `help`
- `pins`
- `status`
- `power-on`
- `power-off`
- `release`
- `tap-reset`
- `read-jtagid`
- `bypass-test`
- `sync-por`
- `read-mem16 <addr>`
- `quick-read16 <addr> <words>`
- `write-read-mem16 <addr> <value>`
- `fram-smoke16 <addr> <value>`
- `fram-bench <addr> <words>`
- `clock-test [cycles]`
- `slot-test <tms> <tdi>`

`slot-test` is only a waveform/debug probe. Its sampled return value is undefined unless the target is already in a valid JTAG state and expecting that bit.

## Build Notes

The repository expects the Pico SDK and ARM embedded toolchain to be available in the shell environment.

This machine now has a clean Pico toolchain install rooted at `C:\Users\passp\pico`:

- SDK: `C:\Users\passp\pico\pico-sdk-2.2.0`
- ARM GNU Toolchain: `C:\Users\passp\pico\bin`
- `pioasm`: `C:\Users\passp\pico\pico-sdk-tools-2.2.0\pioasm`
- `picotool`: `C:\Users\passp\pico\picotool-2.2.0-a4\picotool`

To use that install in PowerShell:

- run `. .\tools\pico-env.ps1`
- configure with `cmake --preset pico-clean`
- build with `cmake --build --preset pico-clean`

The current verified output path is `build-clean\pi_pico_sbw.uf2`.

## MicroPython Native Module Path

There is now a parallel MicroPython-oriented path in `mpy/`.

- `mpy/native/sbw_native.c` is a native `.mpy` module that keeps the hot SBW/JTAG path in C with direct MMIO GPIO writes and fixed `125 MHz` cycle-counted delays
- `mpy/sbw.py` is a thin Python wrapper that configures the SIO pins, checks that `machine.freq()` is exactly `125 MHz`, and calls coarse native operations like `read_id`, `write_mem16`, `read_block16`, and `write_block16`
- `mpy/main.py` provides a small MicroPython shell with the same high-level bench flow as the Pico SDK shell
- `mpy/testsuite.py` provides the parity regression sequence for the MicroPython path

This path deliberately avoids `PIO` and keeps Python out of the timing-critical JTAG inner loops.

One practical note from live bench testing on official MicroPython `v1.27.0` for `RPI_PICO2_W`: the board booted at `150 MHz`, not `125 MHz`. The Python wrapper intentionally checks for `125 MHz` and raises if the clock is different, so a real fixture image should set `machine.freq(125000000)` in `boot.py` before importing the SBW wrapper.
