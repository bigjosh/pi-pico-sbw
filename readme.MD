# Pi Pico SBW Programmer

This repo contains a standalone `MSP430FR4133` `SBW` programmer built around a `Raspberry Pi Pico 2 W` running MicroPython.

The active architecture is:

- MicroPython for fixture orchestration
- a native `.mpy` module in C for all timing-critical `SBW` and `JTAG` work
- direct MMIO writes to `RP2350` `SIO` registers (base address hardcoded in C)
- no `PIO`

The project is intentionally narrow. It is specialized for the exact target and programming flow we have been validating on the bench.

## Current State

The live, bench-validated path is the MicroPython app at the repo root, backed by the native-module build area in [mpy/](mpy/).

The repo also contains the production-oriented TSL programmer app:

- local TI-TXT firmware asset: [tsl-calibre-msp.txt](tsl-calibre-msp.txt)
- programming workflow: [program.py](program.py)
- boot entrypoint: [main.py](main.py)
- retained bench shell: [debug_shell.py](debug_shell.py)

Latest verified hardware results on the wired target:

- `read-jtagid` -> `0x98`
- `bypass-test` -> `0x52AD`
- `sync-por` -> `0xC301`
- `read-mem16 0x2000` -> `0x1234` after regression RAM write
- `read-mem16 0x2002` -> `0xA55A` after regression RAM write
- `read-block16 0x1a00 11` -> descriptor span with `0x0606` at `0x1A00` and `0x0811` at `0x1A14`
- RAM write/readback passes at `0x2000` and `0x2002`
- Python memory smoke write/read/restore passes at `0xC400`
- Python round-trip block benchmark for `10,240` bytes:
  - `~112 ms` per write pass
  - `~114 ms` per read pass
  - `~90 KiB/s` write throughput
  - `~88 KiB/s` read throughput

## Repo Layout

- [mpy/sbw_native.c](mpy/sbw_native.c)
  Native MicroPython module. Owns the `SBW` transport, `JTAG` sequencing, and general target memory read/write primitives. SIO base address is hardcoded; GPIO pin masks are passed from Python.
- [mpy/SBW_NATIVE_API.md](mpy/SBW_NATIVE_API.md)
  Reference for the public native-module API exposed to Python.
- [sbw_config.py](sbw_config.py)
  Hardware constants, pin assignments, regression constants, and the `150 MHz` clock setup/verification logic.
- [sbw.py](sbw.py)
  Thin Python wrapper around the native module plus target power control and byte-level read/write helpers.
- [program.py](program.py)
  Production TSL programming app. Loads the TI-TXT image, writes the timestamp and firmware blocks separately, verifies each block by readback, and power-cycles the target to run it briefly.
- [main.py](main.py)
  Boot entrypoint for the production programmer loop.
- [debug_shell.py](debug_shell.py)
  Interactive bench shell preserved from the earlier development workflow.
- [testsuite.py](testsuite.py)
  Regression and Python-side benchmark entry points.
- [mpy/README.md](mpy/README.md)
  Native-module build and deployment notes.
- [tools/build-mpy-native.ps1](tools/build-mpy-native.ps1)
  Windows helper to build the native `.mpy`.
- [status.md](status.md)
  Current project status and file-by-file handoff.

## Hardware

Bench wiring used for the validated path:

- `GP2` -> target `TEST / SBWTCK`
- `GP3` -> target `/RST/NMI / SBWTDIO`
- `GP4` -> target `VCC` for lab bring-up power switching
- Pico `GND` -> target `GND`

Important constraints:

- `GP4` target power is a bring-up convenience, not a production-grade supply rail
- `SBWTDIO` currently has no series resistor, so the native transport only releases it for the TDO slot itself and for explicit session boundary handling

## Clock Requirement

The native module is compiled against a fixed maximum system clock of `150 MHz`.

The important rule is:

- actual `clk_sys` must be `<= sbw_native.SYS_CLK_HZ`

If the MCU is clocked faster than the compile-time assumption, the native busy-wait loops become too short in real time and `SBW` pulse widths may violate protocol minimums. A slower clock is safe for protocol timing, but will reduce throughput.

The caller is responsible for enforcing that precondition before using the native API. The current `SBWNative()` wrapper does this conservatively by attempting to set the clock to `150 MHz` and verifying the result.

Example for low-level callers:

```python
import machine
import sbw_native

target_hz = sbw_native.SYS_CLK_HZ
machine.freq(target_hz)

actual = machine.freq()
if isinstance(actual, tuple):
    actual = actual[0]
actual = int(actual)

if actual > target_hz:
    raise RuntimeError("clk_sys too fast for sbw_native: %d > %d" % (actual, target_hz))
```

## Build And Deploy

Build the native module on Windows with:

```powershell
python -m pip install --user pyelftools
.\tools\build-mpy-native.ps1
```

Then copy these files to the Pico running MicroPython:

- `mpy/sbw_native.mpy`
- `sbw_config.py`
- `sbw.py`
- `program.py`
- `main.py`
- `debug_shell.py`
- `testsuite.py`
- `tsl-calibre-msp.txt`

On boot, `main.py` starts the production TSL programmer loop automatically.

For the retained debug shell:

```python
import debug_shell
debug_shell.repl()
```

## Primary References

The implementation was built from TI primary documentation in [docs/](docs/):

- `Programming over JTAG` (`SLAU320`)
- `MSP430FR4xx and MSP430FR2xx Family User's Guide` (`SLAU445`)
- `MSP430FR4133` datasheet
