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
6. `import program.py`.
7.  Press **spacebar** to program, any other key to exit to the REPL

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

The example `program.py` reads  `program.txt` from the Pico's filesystem.
This file is in the standard TI-TXT firmware file format. 
Copy your firmware image to the Pico as `program.txt` before running.

## Extensions

The Python harness is designed to be extended. These examples are included
in the repo and can be mixed and matched.

### Auto-detect programming

Fully hands-free. Detects when a target is plugged in and immediately starts programming cycle. Super fast for a production line. 

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

### Google Sheets logging

Log each programming event (device UUID, timestamp, firmware hash, MAC
address, pass/fail) to a Google Sheet via WiFi. Requests are queued to
local files first, so nothing is lost if WiFi is temporarily down.
Uses the built-in WiFi on a Pico W or Pico 2 W.

This also uses SNTP to set the lock clock so that log entries have a local timestamp. 

**Extra files:** `log.py`, `wifi.py`, `secrets.py`

**Quick setup:**

1. Make a copy of the
   [sample spreadsheet](https://docs.google.com/spreadsheets/d/1FnmMkalKIRswcSzWMTtvnYA1gJqtGSZJb9MsDqMPEFU)
   (includes the Apps Script)
2. Deploy the Apps Script as a web app (**Extensions → Apps Script → Deploy**)
3. Create `secrets.py` on the Pico with your WiFi credentials and the
   deployment URL

**Usage:**

```python
import wifi
from log import log_row, flush_once, pending_count
from secrets import WIFI_SSID, WIFI_PASSWORD, SHEETS_URL

wifi.connect(WIFI_SSID, WIFI_PASSWORD)
log_row(SHEETS_URL, [uuid, timestamp, fw_hash, mac, "pass"])
while pending_count():
    flush_once(SHEETS_URL)
```

**Example `secrets.pi`:**
```
WIFI_SSID = "mynetworkiscool"
WIFI_PASSWORD = "shhhhhhhhh"
WIFI_COUNTRY = "US"

# This is the URL of the Google Apps Script web app that receives log rows from the factory programmer
# The sheet is here so you can see if your logging is working: https://docs.google.com/spreadsheets/d/1iP67tK8HS1eW9TOhgdxXeeBYk5jmoF0EW1KPMRkxliM/edit?usp=sharing
# This is just an example sheet I made for testing purposes, you should make your own if you want to do this kind of logging.
SHEETS_URL = "https://script.google.com/macros/s/AKfycbwbaCnzd9HzcxFH7fZNXO6tcGLlXIYpXSt6CnHwemSeGESorucdWF4D7nVO9pjRfy0n/exec"
```

See [web_logging.md](web_logging.md) for full setup instructions, the
Apps Script source, and limitations.

### LPM current measurement

Verify the target entered low-power mode after programming by measuring
its current draw. Very handy for proving out a PCB doesn't have any soft (or
hard) shorts. 

Uses an external 100kΩ burden resistor between a
drive pin (GP21) and the VCC pin (GP27). The drive pin powers the target
through the resistor while the VCC pin reads the voltage drop via ADC.

Usable range is roughly 0.5–25 µA. No per-board calibration needed.

Note that your firmware must put the MSP430 into LPM after boot for this to be useful.

**Extra hardware:** 100kΩ resistor (1% tolerance) between GP21 and GP27

**Measure** (after programming, target still connected):

```python
from target_power import TargetPower

power = TargetPower(27)
i_min, i_avg, i_max = power.measure_current_ua(res_pin=21, r_ext=100_000, vcc=3.32)
print("Current: min=%.2f avg=%.2f max=%.2f µA" % (i_min, i_avg, i_max))
```

See [current_measure.md](current_measure.md) for full details on the
approach, circuit diagram, and why the external resistor is needed.

### WS2812B pixel strip

Visual status indicator for headless operation.

Very handy for headless operation. When combined with auto-detect, you can make
a very efficient production programming station.

**Extra files:** `pixels.py`

**Default Wiring:** GP28 → strip data in, 3V3 → strip VCC, GND → strip GND. 
Note you can move the data pin to any GPIO, and strip ground to any ground pin, but power must be the 3.3V pin (or the VBUS if connected to use power and you want LEDs to be really bright - but this might be flakey with some strips).

**Usage:**

```python
from pixels import Pixels, RED, GREEN, BLUE, BLACK, LT_GREEN, rgb

px = Pixels(28)                             # GP28, PIO state machine 0
px.show([RED, GREEN, BLUE, BLACK, BLACK])   # set 5 pixels
px.show([BLACK] * 5)                        # all off
px.show([LT_GREEN] * 5)                     # all dim green
px.show([BLACK] * 10 )                      # good night
```

Includes 16 saturated color constants plus `LT_` variants (25% brightness).
See [pixels.md](pixels.md) for full API and PIO implementation details.

### Online firmware update

Coming soon.

You will be able to push the button on the Pico to tell it to download the latest version of the firmware image from a github release. 

I just really want to understand how that button interacts with a running python 
program before I put this into the code, because based on a strict reading of the docs it seems like pressing the button might sometimes rarely crash the running code???


## Hardware

My SBW and pixel strip connections:

| Pico Pin | GPIO | Function | Needed for |
|----------|------|----------|------------|
| 28 | GND | SBW GND | SBW (always) |
| 29 | GP22 | SBWTDIO | SBW (always) |
| 31 | GP26 | SBWTCK | SBW (always) |
| 32 | GP27 | VCC | Power target, current measurement |
| 27 | GP21 | Burden resistor | Current measurement |
| 33 | GND | Pixel GND | Pixel strip |
| 34 | GP28 | Pixel data | Pixel strip |
| 36 | 3V3 | Pixel VCC | Pixel strip |

Note that the VCC connection is optional if your target needs external power. The programming code
examples use the VCC pin to power cycle the target, but this is not necessary.

## My actual production programming loop

Check out `program_tsl.py` to see how I actually program [Time Since Launch](https://cwandt.com/products/time-since-launch?srsltid=AfmBOor4auA2sgjH6NeBiJE7tu6mlDL5k5cs1GOXVR0JBQ7_Gj1TFE_N) units in the factory.

It demonstrates:

- auto-detect of target
- target power measurement
- reading MSP430 device serial number
- pixel display for headless operation
- online logging to Google spreadsheet
- downloads latest firmware image from GitHub release on startup

You will probably not be able to use this directly, but it should give you some ideas of what's possible.

# Implementation details
- MicroPython for orchestration
- native `.mpy` module in C for timing-critical `SBW`/`JTAG` bit-banging
- direct MMIO to `RP2350` `SIO` registers
- `~90 KiB/s` write, `~88 KiB/s` read throughput

## How so much faster than TI Tools?

1. Used the quick versions of read and write block. These are only semi-documented and not used by the TI code. These save having to reload the address for every word transferred.
2. Run closer to the max 10Mhz clock speed. We get about 50% of the way there using carefully crafted SIO bitbanging. We could get to 100% with PIO but this was fast enough for me so not worth the hassle.
3. Got rid of all the crufty overhead of the TI tools doing all sorts of stuff over the USB link and doing extra resets of the target etc. We only do what is strictly necessary to reliably program and verify, and we do it the most direct way possible.

## Repo Layout

**Programming examples:**
- [program.py](program.py) — simple manual keypress-triggered programmer
- [program_auto.py](program_auto.py) — auto-detecting programmer
- [program_tsl.py](program_tsl.py) — full production programmer with WiFi logging, current measurement, and pixel status

**Core SBW:**
- [sbw.py](sbw.py) — Python wrapper around the native SBW module
- [mpy/SBW_NATIVE_API.md](mpy/SBW_NATIVE_API.md) — native module API reference
- [mpy/README.md](mpy/README.md) — native module build notes

### Options

- [target_power.py](target_power.py) — target power control, auto-detect, and current measurement
- [utils.py](utils.py) — TI-TXT parsing, firmware hashing, device UUID extraction

**Logging**
- [wifi.py](wifi.py) — WiFi connection management with reconnect
- [log.py](log.py) — offline-resilient Google Sheets logging via file queue
- [safe_time.py](safe_time.py) — thread-safe time/NTP access
- [web_logging.md](web_logging.md) — Google Sheets logging setup guide

**Current measurement**
- [current_measure.md](current_measure.md) — external burden resistor approach and circuit
- [target_power.py](target_power.py) — target power control, auto-detect, and current measurement

**Pixels:**
- [pixels.py](pixels.py) — WS2812B pixel driver via PIO
- [pixels.md](pixels.md) — pixel API and PIO implementation details
- [pixel_test.py](pixel_test.py) — interactive pixel strip test

**Testing and tools:**
- [testsuite.py](testsuite.py) — SBW regression tests and benchmark
- [sbw_config.py](sbw_config.py) — regression test constants
- [power_detect_test.py](power_detect_test.py) — interactive target detect/disconnect test loop
- [tools/build-mpy-native.ps1](tools/build-mpy-native.ps1) — Windows native module build helper


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
