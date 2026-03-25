# MicroPython SBW Fixture

This directory is the MicroPython-facing version of the project.

- `sbw_native.mpy` is the native JTAG engine.
- `sbw.py` is the thin Python wrapper.
- `main.py` is a small serial shell.
- `testsuite.py` is the parity regression runner.

## Design

Python owns fixture orchestration and policy.

The hot SBW/JTAG path stays inside the native module:

- direct MMIO writes to SIO GPIO registers
- fixed compile-time timing
- PRIMASK-based interrupt masking during critical low pulses
- full session bring-up/retry/release inside each coarse native call
- `read_block16` uses the native quick block-read path
- `write_block16` uses the native quick FRAM block-write path when the address range is in FRAM

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

The native module assumes a `150 MHz` system clock.

`sbw.py` calls `machine.freq(150000000)` during `SBWNative()` startup, reads the clock back, and raises if the board did not actually take that setting.

## Build

1. Install `pyelftools` on the build machine.
2. From the repo root, build the natmod:

```powershell
python -m pip install --user pyelftools
.\tools\build-mpy-native.ps1
```

The build output is `mpy/native/sbw_native.mpy`.

Copy these files to the Pico running MicroPython:

- `mpy/native/sbw_native.mpy`
- `mpy/sbw_config.py`
- `mpy/sbw.py`
- `mpy/main.py`
- `mpy/testsuite.py`

Then run:

```python
import main
main.repl()
```
