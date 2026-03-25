# Pi Pico SBW Programmer

This repo contains a standalone `MSP430FR4133` `SBW` programmer built around a `Raspberry Pi Pico 2 W` running MicroPython.

The active architecture is:

- MicroPython for fixture orchestration
- a native `.mpy` module in C for all timing-critical `SBW` and `JTAG` work
- direct MMIO writes to `RP2350` `SIO` registers
- no `PIO`

The project is intentionally narrow. It is specialized for the exact target and programming flow we have been validating on the bench.

## Current State

The live, bench-validated path is the MicroPython path in [mpy](D:/Github/pi-pico-sbw/mpy).

Latest verified hardware results on the wired target:

- `read-jtagid` -> `0x98`
- `bypass-test` -> `0x52AD`
- `sync-por` -> `0xC301`
- `read-mem16 0xfffe` -> `0xEDC8`
- `read-mem16 0xfffc` -> `0xEE44`
- RAM write/readback passes at `0x2000` and `0x2002`
- FRAM smoke write/read/restore passes at `0xC400`
- `fram-bench` for `10,240` bytes returned `1,286,720 us` write and `237,887 us` verify

That is about:

- `7 KiB/s` write throughput
- `42 KiB/s` verify throughput

## Repo Layout

- [mpy/native/sbw_native.c](D:/Github/pi-pico-sbw/mpy/native/sbw_native.c)
  Native MicroPython module. Owns the `SBW` transport, `JTAG` sequencing, memory read/write, FRAM smoke, and FRAM benchmark.
- [mpy/sbw_config.py](D:/Github/pi-pico-sbw/mpy/sbw_config.py)
  Hardware constants, `RP2350` `SIO` MMIO addresses, regression constants, and the `150 MHz` clock setup/verification logic.
- [mpy/sbw.py](D:/Github/pi-pico-sbw/mpy/sbw.py)
  Thin Python wrapper around the native module plus target power control.
- [mpy/main.py](D:/Github/pi-pico-sbw/mpy/main.py)
  Interactive shell for bench use.
- [mpy/testsuite.py](D:/Github/pi-pico-sbw/mpy/testsuite.py)
  Regression and benchmark entry points.
- [mpy/README.md](D:/Github/pi-pico-sbw/mpy/README.md)
  MicroPython build and deployment notes.
- [tools/build-mpy-native.ps1](D:/Github/pi-pico-sbw/tools/build-mpy-native.ps1)
  Windows helper to build the native `.mpy`.
- [status.md](D:/Github/pi-pico-sbw/status.md)
  Current project status and file-by-file handoff.

## Hardware

Bench wiring used for the validated path:

- `GP2` -> target `TEST / SBWTCK`
- `GP3` -> target `/RST/NMI / SBWTDIO`
- `GP4` -> target `VCC` for lab bring-up power switching
- Pico `GND` -> target `GND`

Important constraints:

- `GP4` target power is a bring-up convenience, not a production-grade supply rail
- `SBWTDIO` currently has no series resistor, so the firmware leaves it as input except during intentional drive windows

## Clock Requirement

The native module assumes a `150 MHz` system clock.

At `SBWNative()` startup, the Python wrapper calls `machine.freq(150000000)`, reads the actual clock back, and raises if the board did not take that setting.

## Build And Deploy

Build the native module on Windows with:

```powershell
python -m pip install --user pyelftools
.\tools\build-mpy-native.ps1
```

Then copy these files to the Pico running MicroPython:

- `mpy/native/sbw_native.mpy`
- `mpy/sbw_config.py`
- `mpy/sbw.py`
- `mpy/main.py`
- `mpy/testsuite.py`

For an interactive shell:

```python
import main
main.repl()
```

## Primary References

The implementation was built from TI primary documentation in [docs](D:/Github/pi-pico-sbw/docs):

- `Programming over JTAG` (`SLAU320`)
- `MSP430FR4xx and MSP430FR2xx Family User's Guide` (`SLAU445`)
- `MSP430FR4133` datasheet
