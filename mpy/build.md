# Building `sbw_native.mpy` From Scratch

This document is the full build handoff for the native MicroPython module
[sbw_native.mpy](mpy/sbw_native.mpy).

It is written for someone new to this project who needs to reproduce the build
on a fresh Windows machine.

## What This Build Is

This project does **not** build a Pico SDK firmware image.

It builds a **MicroPython native module** (`.mpy`) that is loaded by a Pico
already running MicroPython. The timing-critical SBW/JTAG code lives in:

- [sbw_native.c](mpy/sbw_native.c)

The output artifact is:

- [sbw_native.mpy](mpy/sbw_native.mpy)

The Python-side app files that use this module live at the repo root:

- [sbw.py](sbw.py)
- [sbw_config.py](sbw_config.py)
- [program.py](program.py)
- [main.py](main.py)
- [debug_shell.py](debug_shell.py)
- [testsuite.py](testsuite.py)

## Important Architectural Notes

- No Pico SDK is required to build `sbw_native.mpy`.
- No CMake is required.
- The build uses MicroPython's `dynruntime.mk` support.
- The current native module targets `ARCH = armv7m` as defined in [Makefile](mpy/Makefile).
- The tested runtime target is official MicroPython on `RPI_PICO2_W`.

## Tested Working Build Environment

This repo is currently known to build with:

- Windows PowerShell
- a recent CPython 3 install
- `pyelftools`
- an `arm-none-eabi` GNU toolchain
- an MSYS2-compatible `bash` with `make`
- a local MicroPython source tree containing `py/dynruntime.mk`

Current local reference points on this machine:

- MicroPython source tree:
  - `D:\Github\micropython-tmp`
  - current checked-out commit when this doc was written: `5c00edc`
- ARM toolchain binaries:
  - `C:\Users\passp\pico\bin`
- Bash used by the helper:
  - `D:\devkitPro\msys2\usr\bin\bash.exe`

You do **not** need to match those exact install locations, but if you do not,
you must override the helper script arguments or invoke `make` manually.

## Prerequisites

### 1. Clone This Repo

Clone the repo wherever you want. This document assumes:

- repo root: wherever you cloned this repo (examples below use `D:\Github\pi-pico-sbw`)

### 2. Install Python

Install a normal CPython for Windows.

Then install the only Python-side build dependency:

```powershell
python -m pip install --user pyelftools
```

Why this is needed:

- MicroPython's native-module link step uses `pyelftools`

Quick verification:

```powershell
python -m pip show pyelftools
```

### 3. Install an ARM GNU Toolchain

You need a toolchain that provides:

- `arm-none-eabi-gcc`
- `arm-none-eabi-ld`
- `arm-none-eabi-objcopy`

Any reasonable modern `arm-none-eabi` toolchain should work as long as it can
target Cortex-M / ARMv7-M code.

This project has been built successfully with the Raspberry Pi Pico Windows
toolchain bundle under:

- `C:\Users\passp\pico\bin`

Quick verification:

```powershell
Get-ChildItem C:\Users\passp\pico\bin\arm-none-eabi-gcc*
```

If your toolchain is installed elsewhere, that is fine. You will pass the path
into the build helper later.

### 4. Install `bash` + `make`

The helper script runs `make` through `bash`.

The currently tested path is:

- `D:\devkitPro\msys2\usr\bin\bash.exe`

Any MSYS2-style environment is fine as long as:

- `bash` runs
- `make` is available inside it

Quick verification:

```powershell
& "D:\devkitPro\msys2\usr\bin\bash.exe" -lc "make --version"
```

### 5. Get a MicroPython Source Tree

This build depends on MicroPython's dynamic runtime build support:

- `py/dynruntime.mk`
- `py/dynruntime.h`

The Makefile in this repo defaults to:

- `MPY_DIR ?= ../../micropython-tmp`

Because [Makefile](mpy/Makefile) lives in `mpy/`, that
default resolves to a sibling checkout:

- `D:\Github\micropython-tmp`

So the easiest setup is:

1. Clone MicroPython next to this repo
2. Name that checkout `micropython-tmp`

Example:

```powershell
cd D:\Github
git clone https://github.com/micropython/micropython.git micropython-tmp
```

Quick verification:

```powershell
Test-Path D:\Github\micropython-tmp\py\dynruntime.mk
Test-Path D:\Github\micropython-tmp\py\dynruntime.h
```

Important note:

- `.mpy` compatibility depends on MicroPython's `_mpy` version and target arch.
- In practice, you should build against a MicroPython source tree that is close
  to the firmware running on the board.
- This project has been tested with official MicroPython `v1.27.0` on
  `RPI_PICO2_W`.

## Files That Control the Build

- [Makefile](mpy/Makefile)
  - declares `MOD = sbw_native`
  - declares `SRC = sbw_native.c`
  - declares `ARCH = armv7m`
  - includes `$(MPY_DIR)/py/dynruntime.mk`

- [build-mpy-native.ps1](tools/build-mpy-native.ps1)
  - Windows helper wrapper
  - launches `make` from PowerShell through `bash`
  - lets you override:
    - Python executable
    - Bash executable
    - ARM toolchain binary directory

## Standard Build Path

From the repo root:

```powershell
python -m pip install --user pyelftools
.\tools\build-mpy-native.ps1
```

If everything is set up correctly, the output appears at:

- [sbw_native.mpy](mpy/sbw_native.mpy)

Intermediates are written under:

- `mpy/build/`

## Build Helper Arguments

If your machine does not use the current default paths, override them:

```powershell
.\tools\build-mpy-native.ps1 `
  -PythonExe "C:/Path/To/python.exe" `
  -BashExe "D:/Path/To/bash.exe" `
  -ArmBinDir "C:/Path/To/arm-toolchain/bin"
```

Current default values are defined in:

- [build-mpy-native.ps1](tools/build-mpy-native.ps1)

## Manual Build Path

If the helper script is inconvenient, you can invoke the same build manually.

From PowerShell:

```powershell
& "D:\devkitPro\msys2\usr\bin\bash.exe" -lc `
  "export PATH=/C/Users/passp/pico/bin:`$PATH; make -C /D/Github/pi-pico-sbw/mpy PYTHON=/C/Users/passp/AppData/Local/Python/pythoncore-3.14-64/python.exe"
```

If your MicroPython checkout is not at the default sibling path, pass `MPY_DIR`
explicitly:

```powershell
& "D:\devkitPro\msys2\usr\bin\bash.exe" -lc `
  "export PATH=/C/Users/passp/pico/bin:`$PATH; make -C /D/Github/pi-pico-sbw/mpy PYTHON=/C/Path/To/python.exe MPY_DIR=/D/Somewhere/micropython"
```

Notes:

- paths inside the `bash -lc` string must use POSIX-style slash conversion
- drive letters become `/C/...`, `/D/...`, etc.

## Clean Rebuild

To force a clean rebuild, remove:

- `mpy/build/`
- `mpy/sbw_native.mpy`

Example:

```powershell
cmd /c rmdir /s /q mpy\build
del mpy\sbw_native.mpy
.\tools\build-mpy-native.ps1
```

## Deploying the Built Module

The board must already be running MicroPython.

Minimum files to copy after rebuilding:

- `mpy/sbw_native.mpy`
- `sbw.py`
- `sbw_config.py`
- `program.py`
- `main.py`
- `debug_shell.py`
- `testsuite.py`
- `tsl-calibre-msp.txt`

Example with `mpremote`:

```powershell
mpremote connect COM11 cp .\mpy\sbw_native.mpy :sbw_native.mpy
mpremote connect COM11 cp .\sbw.py :sbw.py
mpremote connect COM11 cp .\sbw_config.py :sbw_config.py
mpremote connect COM11 cp .\program.py :program.py
mpremote connect COM11 cp .\main.py :main.py
mpremote connect COM11 cp .\debug_shell.py :debug_shell.py
mpremote connect COM11 cp .\testsuite.py :testsuite.py
mpremote connect COM11 cp .\tsl-calibre-msp.txt :tsl-calibre-msp.txt
```

Practical note:

- the Pico may momentarily reset or re-enumerate after file copies
- if `COM11` disappears briefly, wait a second and reconnect

## Post-Build Verification

Once deployed, a good sanity check is:

```powershell
mpremote connect COM11 exec "import testsuite; print('regression', testsuite.run_regression()); print('bench', testsuite.run_bench())"
```

Current known-good shape on this project is:

- regression returns `True`
- benchmark returns a tuple like:
  - `(True, (5120, ..., ..., ..., ...))`

## Common Failure Modes

### `dynruntime.mk` not found

Cause:

- `MPY_DIR` is wrong
- MicroPython source tree is missing

Fix:

- clone MicroPython to `../micropython-tmp`
- or override `MPY_DIR` in the manual build

### `arm-none-eabi-gcc` not found

Cause:

- ARM toolchain is missing from the `PATH` seen by `bash`

Fix:

- install the toolchain
- pass the correct `-ArmBinDir` to [build-mpy-native.ps1](tools/build-mpy-native.ps1)

### `make` not found

Cause:

- wrong `bash` path
- MSYS2/devkitPro shell not installed

Fix:

- install an MSYS2-style environment
- verify `bash -lc "make --version"` works

### `pyelftools` missing

Cause:

- Python dependency not installed

Fix:

```powershell
python -m pip install --user pyelftools
```

### `.mpy` imports fail on the board

Cause:

- built against an incompatible MicroPython version / `_mpy` format
- wrong target architecture

Fix:

- rebuild against a MicroPython source tree close to the firmware actually on
  the Pico
- keep `ARCH = armv7m` for the RP2350 ARM core path used by this project

## Quick Checklist

For a fresh machine:

1. Clone this repo.
2. Clone MicroPython to a sibling folder named `micropython-tmp`.
3. Install CPython.
4. Install `pyelftools`.
5. Install an `arm-none-eabi` toolchain.
6. Install `bash` + `make`.
7. Run [build-mpy-native.ps1](tools/build-mpy-native.ps1).
8. Confirm [sbw_native.mpy](mpy/sbw_native.mpy) exists.
9. Copy the module plus the root Python files to the Pico.
10. Run [testsuite.py](testsuite.py) on the board.
