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

### Google Sheets logging

Log each programming event (device UUID, timestamp, firmware hash, MAC
address, pass/fail) to a Google Sheet via WiFi. Requests are queued to
local files first, so nothing is lost if WiFi is temporarily down.
Uses the built-in WiFi on a Pico W or Pico 2 W.

**Extra files:** `addrow.py`, `wifi.py`, `secrets.py`

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
from addrow import add_row, flush_once, pending_count
from secrets import WIFI_SSID, WIFI_PASSWORD, SHEETS_URL

wifi.connect(WIFI_SSID, WIFI_PASSWORD)
add_row(SHEETS_URL, [uuid, timestamp, fw_hash, mac, "pass"])
while pending_count():
    flush_once(SHEETS_URL)
```

See [web_logging.md](web_logging.md) for full setup instructions, the
Apps Script source, and limitations.

### LPM current measurement

Verify the target entered low-power mode after programming by measuring
its current draw — no external sense hardware needed. Uses the Pico's
internal pull-up resistor (~66kΩ) on the VCC pin as a current sense
element and reads the voltage drop via ADC.

Usable range is roughly 0.5–30 µA. Overcurrent protection monitors the
voltage during the switchover from GPIO drive to pull-up and automatically
rescues back to full power if the target is drawing too much.

Requires a one-time calibration with a known resistor (~1MΩ) to
characterize the pull-up resistance.

**Extra files:** `current_measure_calibrate.py`, `calibration.json` (generated)

**Calibrate** (connect 1MΩ from GP28 to GND, run once):

```python
import current_measure_calibrate
```

**Measure** (after programming, target still connected):

```python
from target_power import TargetPower
r_pullup = TargetPower.load_calibration()
power.power_via_pullup()
time.sleep_ms(200)  # let RC settle
current = power.measure_current_ua(r_pullup)
if current is None:
    print("Brownout — target not in LPM")
else:
    print("Target current: %.1f µA" % current)
```

See [current_measure.md](current_measure.md) for full details on the
approach, calibration math, limitations, and a future capacitor-discharge
method.

### WS2812B pixel strip

Visual status indicator for headless operation. 

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

## Hardware

My SBW and pixel strip connections:

| Pico Pin | GPIO | Function |
|----------|------|----------|
| 28 | GND | SBW GND |
| 29 | GP22 | SBWTDIO |
| 31 | GP26 | SBWTCK |
| 32 | GP27 | VCC |
| 33 | GND | Pixel GND |
| 34 | GP28 | Pixel data |
| 36 | 3V3 | Pixel VCC |

GP26 (SBWTCK) doubles as ADC input for target auto-detect.
GP27 (VCC) doubles as ADC input for LPM current measurement.
The pixel strip is optional. VCC is optional if your target has external power.

Note that the VCC connection is optional if your target needs external power. The programming code
examples use the VCC pin to power cycle the target, but this is not necessary.

## My actual production programming loop

Check out `program_tsl.py` to see how I actually program [Time Since Launch](https://cwandt.com/products/time-since-launch?srsltid=AfmBOor4auA2sgjH6NeBiJE7tu6mlDL5k5cs1GOXVR0JBQ7_Gj1TFE_N) units in the factory.

It demonstrates auto-detect, power measurement, real-time device commissioning, headless pixel display, and online logging.

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
