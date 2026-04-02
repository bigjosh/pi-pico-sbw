# Native Module Build

This directory contains the native-module source, build metadata, and
generated `.mpy` output used by the MicroPython app at the repo root.

For the full scratch-build handoff, including prerequisite installs and manual
build fallback steps, see [build.md](build.md).

Contents:

- `sbw_native.c`: native SBW/JTAG implementation
- `Makefile`: MicroPython `dynruntime.mk` build rules
- `SBW_NATIVE_API.md`: Python-facing native API reference
- `sbw_native.mpy`: generated native module output
- `build/`: generated intermediate build files

The Python-side app files live at the repo root:

- [sbw.py](../sbw.py)
- [sbw_config.py](../sbw_config.py)
- [program.py](../program.py)
- [main.py](../main.py)
- [debug_shell.py](../debug_shell.py)
- [testsuite.py](../testsuite.py)
- [tsl-calibre-msp.txt](../tsl-calibre-msp.txt)

## Build

1. Install `pyelftools` on the build machine.
2. From the repo root, build the natmod:

```powershell
python -m pip install --user pyelftools
.\tools\build-mpy-native.ps1
```

The build output is `sbw_native.mpy` in this directory.

## Deploy

Copy these files to the Pico running MicroPython:

- `mpy/sbw_native.mpy`
- `sbw_config.py`
- `sbw.py`
- `program.py`
- `main.py`
- `debug_shell.py`
- `testsuite.py`
- `tsl-calibre-msp.txt`

For the native API itself, see [SBW_NATIVE_API.md](SBW_NATIVE_API.md).
