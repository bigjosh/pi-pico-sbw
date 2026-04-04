# Pi Pico SBW Programmer

A standalone MSP430 programmer using `Spy-Bi-Wire` (`SBW`), built on a `Raspberry Pi Pico/2/W` running MicroPython.

Up to 100x faster than TI programmers like MSP-FET. 
Can run stand-alone on a $4 Pi Pico.
Python harness easily extensible. Examples showing target-autodetect & logging to a google spreadsheet. 

## Quick Start

1. Install [MicroPython](https://micropython.org/download/RPI_PICO2W/) on your Pico
2. Wire the four target pins (see [Hardware](#hardware) below)
3. Copy these files to the Pico using [mpremote](https://docs.micropython.org/en/latest/reference/mpremote.html), Thonny, or any file copy tool:
   `mpy/sbw_native.mpy`, `sbw.py`, `target_power.py`, `program.py`, `main.py`
4. Rename your TI-TXT firmware file to `program.txt` and copy to the Pico
5. Connect a target and open a serial terminal to the Pico (or just power it on)
6. `main.py` runs automatically on boot, starting the manual programming loop
7. Press **spacebar** to program, any other key to exit to the REPL

You should see something like this...

```
Loading program.txt into memory...
Firmware is 4 block(s), 10426 bytes.

Press [spacebar] to start programming cycle, any other key to exit...
00.000 Powering on target...
00.020 Reading JTAG ID...
00.066 Writing firmware block 1/4 addr=0x0C400 len=414...
00.115 Writing firmware block 2/4 addr=0x0C5A0 len=9974...
00.227 Writing firmware block 3/4 addr=0x0FF80 len=8...
00.273 Writing firmware block 4/4 addr=0x0FFE2 len=30...
00.320 Verifying firmware blocks...
00.320 Verifying firmware block 1/4 addr=0x0C400 len=414...
00.369 Verifying firmware block 2/4 addr=0x0C5A0 len=9974...
00.483 Verifying firmware block 3/4 addr=0x0FF80 len=8...
00.529 Verifying firmware block 4/4 addr=0x0FFE2 len=30...
00.575 All blocks verified.
00.575 Power off...
00.575 Done.
Press [spacebar] to start programming cycle, any other key to exit...
```

### Firmware image

Our programming examples read  `program.txt` from the Pico's filesystem.
This file is in the standard TI-TXT firmware file format. 
Copy your firmware image to the Pico as `program.txt` before running.

## Extensions

The Python harness is designed to be extended. These examples are included
in the repo and can be mixed and matched.

### Auto-detect programming

Fully hands-free. Detects when a target is plugged in.

**Extra files:** `program_auto.py`

`program_auto.py` is an example of how this works. It programs it
automatically, then waits for removal before accepting the next one.
The onboard LED lights during programming so you know when it's safe
to disconnect.

**To activate on boot**, update `main.py`:

```python
from program_auto import program_loop
program_loop()
```

**Or run from the REPL:**

```python
import program_auto
program_auto.program_loop()
```

### Google Sheets logging: `addrow.py`

Log each programming event (device UUID, timestamp, pass/fail) to a
Google Sheet via a Google Apps Script web app. 

Requests are written to
a local pending file before sending, so nothing is lost if WiFi is
temporarily unavailable. Pending rows are drained in FIFO order on
each call.

Uses the built-in Wifi
on a Pico W or Pico W2.

**Extra files:** `addrow.py`, `wifi.py`, `secrets.py`

**Setup:**

1. Create a Google Apps Script that accepts POST data and appends to a sheet
2. Create `secrets.py` on the Pico (not committed to git):

```python
WIFI_SSID = "YourNetwork"
WIFI_PASSWORD = "YourPassword"
SHEETS_URL = "https://script.google.com/macros/s/YOUR_SCRIPT_ID/exec"
```

**Usage from your program:**

```python
from secrets import WIFI_SSID, WIFI_PASSWORD, SHEETS_URL
from addrow import add_row
import wifi

wifi.connect(WIFI_SSID, WIFI_PASSWORD)
ok = add_row(SHEETS_URL, [uuid, timestamp, "pass"])
```

Returns `True` if all pending rows were sent. If the connection drops,
rows accumulate in `log_pending.json` and drain on the next successful call.

This repo has a sample logging spreadsheet set up that you can use for testing. The app URL is already in `secrets.py` and the
spreadsheet is [here](SHEETS_URL). You should make a copy of that spreadsheet, deploy the webapp, and change the URL if you want to 
use this logging for real on your own system.

## Hardware

Pin connections (four consecutive physical pins on the Pico 2 W):

| Pico Pin | GPIO | Target Pin |
|----------|------|------------|
| 31 | GP26 | SBWTCK |
| 32 | GP27 | SBWTDIO |
| 33 | GND | GND |
| 34 | GP28 | VCC |

Note that the Vcc connection is optional if your target needs external power. The programming code
examples use the Vcc pin to power cycle the target, but this is not necessary.

## My actual production programming loop

Check out `program_tsl.py` to see how I actually program [Time Since Launch](https://cwandt.com/products/time-since-launch?srsltid=AfmBOor4auA2sgjH6NeBiJE7tu6mlDL5k5cs1GOXVR0JBQ7_Gj1TFE_N) units in the factory.

It demonstrates auto-detect, power measurement, real-time device commissioning, and online logging.

It also shows how I made the setup into a standalone programing station that uses colored LEDs to visually show the programming
progress and any errors so no screen is needed. 

You will probably no be able to use this directly, but it should give you some ideas of what's possible.


# Implementation details
- MicroPython for orchestration
- native `.mpy` module in C for timing-critical `SBW`/`JTAG` bit-banging
- direct MMIO to `RP2350` `SIO` registers
- `~90 KiB/s` write, `~88 KiB/s` read throughput

## How so much faster than TI Tools?

1. Used the quick versions of read and write block. These are only semi-documented and not used by the TI code. These save having to reload the address for every word transferred.
2. Run closer to the max 10Mhz clock speed. We get about 50% of the way there using carefully crafted SIO bitbanging. We could get to 100% with PIO but this was fast enough for me.
3. Got rid of all the crufty overhead of the TI tools doing all sorts of stuff over the USB link and doing extra resets of the target etc. We only do what is strictly necessary to reliably program and verify, and we do it the most direct way possible.

## Repo Layout

- [program_auto.py](program_auto.py) — auto-detecting programmer (see above)
- [program.py](program.py) — manual keypress-triggered programmer (see above)
- [sbw.py](sbw.py) — Python wrapper around the native SBW module
- [target_power.py](target_power.py) — target power control and connection detection
- [sbw_native.mpy](mpy/sbw_native.mpy) — compiled native module
- [mpy/sbw_native.c](mpy/sbw_native.c) — native module source (SBW transport, JTAG sequencing, memory access)
- [mpy/SBW_NATIVE_API.md](mpy/SBW_NATIVE_API.md) — native module API reference
- [testsuite.py](testsuite.py) — regression tests and benchmark
- [sbw_config.py](sbw_config.py) — regression test constants
- [power_detect_test.py](power_detect_test.py) — interactive target detect/disconnect test loop
- [mpy/README.md](mpy/README.md) — native module build notes
- [tools/build-mpy-native.ps1](tools/build-mpy-native.ps1) — Windows build helper

## Build And Deploy

Build the native module on Windows:

```powershell
python -m pip install --user pyelftools
.\tools\build-mpy-native.ps1
```

## Clock Requirement

The native module is compiled for a maximum system clock of `150 MHz`. The
`SBW` class checks `machine.freq()` on init and raises if it exceeds
`sbw_native.SYS_CLK_HZ`. Running slower is safe but reduces throughput.

## Primary References

Built from TI primary documentation in [docs/](docs/):

- `Programming over JTAG` (`SLAU320`)
- `MSP430FR4xx and MSP430FR2xx Family User's Guide` (`SLAU445`)
- `MSP430FR4133` datasheet
