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
    power_mask,
)
```

## Clock Assumption

The native module assumes a `125 MHz` system clock.

`sbw.py` calls `machine.freq()` at startup and raises if the clock is anything else.

On the live `RPI_PICO2_W` test board with official MicroPython `v1.27.0`, the default clock came up at `150 MHz`, so `machine.freq(125000000)` must be applied before constructing `SBWNative()`. The clean place to do that in a fixture image is `boot.py`.

## Build

1. Install `pyelftools` on the build machine.
2. Build the natmod:

```powershell
python -m pip install --user pyelftools
.\tools\build-mpy-native.ps1
```

Copy these files to the Pico running MicroPython:

- `mpy/native/sbw_native.mpy`
- `mpy/sbw_config.py`
- `mpy/sbw.py`
- `mpy/main.py`
- `mpy/testsuite.py`

Then run:

```python
import machine
machine.freq(125000000)
import main
main.repl()
```
