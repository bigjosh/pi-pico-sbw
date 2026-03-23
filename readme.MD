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

These choices keep `GP2` and `GP3` adjacent for the future `PIO` transport and leave `GP0` and `GP1` free for UART debugging if needed.

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

Current bench verification on the wired `MSP430FR4133`:

- `read-jtagid` returns `0x98`
- `bypass-test` returns `0x52AD` for the `0xA55A` smoke pattern
- `sync-por` returns `0xC301`, which satisfies the `0x0301` Full-Emulation-State mask
- `read-mem16 0xfffe` consistently returns `0xEDC8`
- `read-mem16 0xfffc` consistently returns `0xEE44`
- `write-read-mem16 0x2000 0x1234` verified RAM write/readback
- `write-read-mem16 0x2002 0xA55A` verified RAM write/readback
- the smoke test passed repeatedly across multiple target power cycles

This repo is now past raw bring-up and has a validated SBW/JTAG control baseline with working RAM access. The next major step is clearing FRAM write protection for FR4xx and then doing the first cautious FRAM write/readback at a known-safe address.

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
- `write-read-mem16 <addr> <value>`
- `clock-test [cycles] [low_us] [high_us]`
- `slot-test <tms> <tdi> [low_us] [high_us] [sample_us]`

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
