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
- externally powered target

Raspberry Pi tested during development:

- Raspberry Pi Pico 2W

### Wiring

That maps to the physical header as:

Pi Data pin TBD  -> target `/RST/NMI/SBWTDIO`
Pi Clock pin TBD  -> target `TEST/SBWTCK`
Pi Ground pin TBD ->  target `GND`

### Power

The target must already have valid `VCC`.

For this FR4133 work:

- `VCC` switching is not required to reach the needed debug/programming states
- only `RST` and `TEST` are actively driven by the Pi
- ground must be shared between Pi and target

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

