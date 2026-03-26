# Native Module Build

This directory now contains only the native-module source, build metadata, and
generated `.mpy` output used by the MicroPython app at the repo root.

Contents:

- `sbw_native.c`: native SBW/JTAG implementation
- `Makefile`: MicroPython `dynruntime.mk` build rules
- `SBW_NATIVE_API.md`: Python-facing native API reference
- `sbw_native.mpy`: generated native module output
- `build/`: generated intermediate build files

The Python-side app files now live at the repo root:

- [sbw.py](D:/Github/pi-pico-sbw/sbw.py)
- [sbw_config.py](D:/Github/pi-pico-sbw/sbw_config.py)
- [program.py](D:/Github/pi-pico-sbw/program.py)
- [main.py](D:/Github/pi-pico-sbw/main.py)
- [debug_shell.py](D:/Github/pi-pico-sbw/debug_shell.py)
- [testsuite.py](D:/Github/pi-pico-sbw/testsuite.py)
- [tsl-calibre-msp.txt](D:/Github/pi-pico-sbw/tsl-calibre-msp.txt)

## Build

1. Install `pyelftools` on the build machine.
2. From the repo root, build the natmod:

```powershell
python -m pip install --user pyelftools
.\tools\build-mpy-native.ps1
```

The build output is [sbw_native.mpy](D:/Github/pi-pico-sbw/mpy/sbw_native.mpy).

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

For the native API itself, see [SBW_NATIVE_API.md](D:/Github/pi-pico-sbw/mpy/SBW_NATIVE_API.md).
