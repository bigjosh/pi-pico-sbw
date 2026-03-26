# MicroPython SBW Fixture

This directory is the MicroPython-facing version of the project.

- `sbw_native.mpy` is the native JTAG engine.
- `sbw.py` is the thin Python wrapper.
- `program.py` is the TSL production programmer flow.
- `main.py` boots straight into the production programmer loop.
- `debug_shell.py` preserves the old bench shell.
- `testsuite.py` is the parity regression runner.
- `tsl-calibre-msp.txt` is the TI-TXT firmware image programmed into the target.

## Design

Python owns fixture orchestration and policy.

The hot SBW/JTAG path stays inside the native module:

- direct MMIO writes to SIO GPIO registers
- fixed compile-time timing
- PRIMASK-based interrupt masking during critical low pulses
- `SBWTDIO` stays actively driven between logical JTAG/SBW bit cycles and is only released for the TDO slot and explicit session boundary handling
- full session bring-up/retry/release inside each coarse native call
- `read_block16` uses the native quick block-read path
- `write_block16` uses the native quick FRAM block-write path when the address range is in FRAM

Benchmark timing and verification policy stays in Python. The current `fram-bench` flow:

- generates a deterministic block in Python
- times the `write_block16` round trip as seen by Python
- times `read_block16`, verifies the returned bytes, then inverts every bit
- repeats the write/read/verify cycle and reports all four timings

The production programming flow also stays in Python. It:

- loads the local `tsl-calibre-msp.txt` image
- parses its contiguous TI-TXT blocks
- writes the commissioning timestamp block at `0x1800`
- writes each firmware block separately with no erase step
- verifies each block by reading it back from the target
- power-cycles the target, leaves it running for `1` second, then powers it down

For `write_block16`, the public contract is that a single block must stay within one protection class:

- RAM/peripheral
- info FRAM
- main FRAM

The hardware descriptor tuple passed to every native call is:

```python
(
    gpio_in_addr,
    gpio_out_set_addr,
    gpio_out_clr_addr,
    gpio_oe_set_addr,
    gpio_oe_clr_addr,
    clock_mask,
    data_mask,
)
```

## Clock Assumption

The native module is compiled against a fixed maximum system clock of `150 MHz`.

The actual runtime requirement is:

- `clk_sys <= sbw_native.SYS_CLK_HZ`

If the board runs faster than that compile-time assumption, the native delay loops produce shorter real pulse widths and `SBW` timing minimums may be violated. Slower clocks are safe for timing but slower overall.

The caller must ensure this before using the native API. The current [sbw.py](D:/Github/pi-pico-sbw/mpy/sbw.py) wrapper does it by setting the clock to `150 MHz` and checking what the board actually took.

Example:

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

## Build

1. Install `pyelftools` on the build machine.
2. From the repo root, build the natmod:

```powershell
python -m pip install --user pyelftools
.\tools\build-mpy-native.ps1
```

The build output is `mpy/native/sbw_native.mpy`.

To deploy the natmod with post-copy SHA-256 verification:

```powershell
.\tools\deploy-native.ps1
```

This script:

- builds `sbw_native.mpy` unless `-SkipBuild` is passed
- copies it to the Pico with `mpremote cp`
- computes `SHA-256` locally and again on the Pico
- hard-fails on any mismatch with no retry

Copy these files to the Pico running MicroPython:

- `mpy/native/sbw_native.mpy`
- `mpy/sbw_config.py`
- `mpy/sbw.py`
- `mpy/program.py`
- `mpy/main.py`
- `mpy/debug_shell.py`
- `mpy/testsuite.py`
- `mpy/tsl-calibre-msp.txt`

For the production programmer loop:

```python
import main
```

For the old debug shell:

```python
import debug_shell
debug_shell.repl()
```
