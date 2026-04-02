# Native API

This file documents the MicroPython native module source and build output for `sbw_native`.

This implements a bit-banged Spy-Bi-Wire driver. It must be in C to meet timing requirements. It does occasionally disable IRQs, but only for the very short SBWTCK low phases.

The intended consumer is the Python layer in [sbw.py](../sbw.py). This file documents the API that the Python side is allowed to rely on.

During an active SBW/JTAG session, the native transport keeps `SBWTDIO` actively driven between logical bit cycles. It only releases the line for the `TDO` slot itself and for explicit session-boundary handling such as `release`/entry.

## Module Name

- import name: `sbw_native`
- build artifact: [sbw_native.mpy](sbw_native.mpy)
- implementation: [sbw_native.c](sbw_native.c)

## Pin Mask Arguments

Every native function takes `clk` and `dio` as its first two arguments:

- `clk`: one-hot bitmask for `SBWTCK`
- `dio`: one-hot bitmask for `SBWTDIO`

The SIO base address (`0xD0000000`) is hardcoded in the native module since it is fixed for the RP2350.

Power control is not part of the native API; Python owns target power switching.

The canonical mask constants live in [sbw_config.py](../sbw_config.py) as `CLOCK_MASK` and `DATA_MASK`.

## Exported Constants

These globals are exported by the native module:

- `SYS_CLK_HZ`

This is an informational convenience constant for Python. It does not change runtime behavior.

## Exported Functions

### `read_id(clk, dio) -> (ok, jtag_id)`

- `clk`: clock pin mask
- `dio`: data pin mask
- returns:
  - `ok`: `bool`
  - `jtag_id`: `int`

Performs SBW session bring-up and reads the JTAG ID.

### `bypass_test(clk, dio) -> (ok, captured)`

- `clk`: clock pin mask
- `dio`: data pin mask
- returns:
  - `ok`: `bool`
  - `captured`: `int`

Runs the BYPASS smoke test using the fixed native pattern.

### `sync_and_por(clk, dio) -> (ok, control_capture)`

- `clk`: clock pin mask
- `dio`: data pin mask
- returns:
  - `ok`: `bool`
  - `control_capture`: `int`

Synchronizes JTAG, executes the POR flow, and returns the captured control signal word.

### `read_mem16(clk, dio, address) -> (ok, value)`

- `clk`: clock pin mask
- `dio`: data pin mask
- `address`: target address
- returns:
  - `ok`: `bool`
  - `value`: `int`

Reads a single 16-bit word using the non-quick path.

### `write_mem16(clk, dio, address, value) -> (ok, readback)`

- `clk`: clock pin mask
- `dio`: data pin mask
- `address`: target address
- `value`: 16-bit word
- returns:
  - `ok`: `bool`
  - `readback`: `int`

Writes a single 16-bit word, reads it back, and returns the readback value.

### `read_block16(clk, dio, address, words) -> (ok, data_bytes)`

- `clk`: clock pin mask
- `dio`: data pin mask
- `address`: start address
- `words`: number of 16-bit words
- returns:
  - `ok`: `bool`
  - `data_bytes`: `bytes`

This is the native block-read API. On FR4133 the underlying Data Quick read
sequence is valid across the target memory map, so this routine is not limited
to FRAM.

### `write_block16(clk, dio, address, data_bytes) -> bool`

- `clk`: clock pin mask
- `dio`: data pin mask
- `address`: start address
- `data_bytes`: even-length bytes-like object containing little-endian 16-bit words
- returns:
  - `bool`

This is the native block-write API.

Behavior:

- for FRAM address ranges, it uses the fast quick block-write path with FRAM
  protection handling
- for non-FRAM writable ranges such as RAM/peripherals, it falls back to the
  standard per-word write path

Public contract:

- the block must not cross a protection-class boundary
- a single block must stay entirely within one of:
  - RAM/peripheral
  - info FRAM
  - main FRAM
- `data_bytes` length must be even

## Ownership Boundary

The native module owns:

- timing-critical SBW slot generation
- JTAG sequencing
- target memory read/write implementation
- generic quick-read mechanics plus FRAM-specific protection and quick-write handling

Python owns:

- target power control
- benchmark timing
- test pattern generation
- buffer verification policy
- interactive shell formatting

So benchmark logic should stay out of `sbw_native.c` unless a future change turns it into a generally useful primitive.

## Error Model

The functions above generally report protocol success with a leading `bool` rather than raising.

The native module raises `ValueError` for clearly invalid caller inputs such as:

- malformed hardware tuple
- zero GPIO masks
- odd-length block write buffer

## Clock Assumption

The native code is compiled for a fixed maximum `150 MHz` system clock.

The actual safety requirement is:

- `clk_sys <= SYS_CLK_HZ`

If the MCU is clocked faster than `SYS_CLK_HZ`, the delay loops in the native module get shorter in real time and can violate `SBW` timing minimums. Running slower than `SYS_CLK_HZ` is timing-safe, but slower overall.

The caller must enforce this before calling the native API. The current enforcement lives in [sbw_config.py](../sbw_config.py) and is called by [sbw.py](../sbw.py).

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
