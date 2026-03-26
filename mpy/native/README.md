# Native API

This folder contains the MicroPython native module source and build output for `sbw_native`.

This implements a bit-banged Spy-Bi-Wire driver. It must be in C to meet timing requirements. It does occasionally disable IRQs, but only for the very short SBWTCK low phases.

The intended consumer is the Python layer in [sbw.py](D:/Github/pi-pico-sbw/mpy/sbw.py). This file documents the API that the Python side is allowed to rely on.

During an active SBW/JTAG session, the native transport keeps `SBWTDIO` actively driven between logical bit cycles. It only releases the line for the `TDO` slot itself and for explicit session-boundary handling such as `release`/entry.

## Module Name

- import name: `sbw_native`
- build artifact: [sbw_native.mpy](D:/Github/pi-pico-sbw/mpy/native/sbw_native.mpy)
- implementation: [sbw_native.c](D:/Github/pi-pico-sbw/mpy/native/sbw_native.c)

## Hardware Descriptor

Every native function takes the hardware descriptor tuple as its first argument.

Tuple layout:

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

Field meanings:

- `gpio_in_addr`: address of the `SIO GPIO_IN` register
- `gpio_out_set_addr`: address of the `SIO GPIO_OUT_SET` register
- `gpio_out_clr_addr`: address of the `SIO GPIO_OUT_CLR` register
- `gpio_oe_set_addr`: address of the `SIO GPIO_OE_SET` register
- `gpio_oe_clr_addr`: address of the `SIO GPIO_OE_CLR` register
- `clock_mask`: one-hot mask for `SBWTCK`
- `data_mask`: one-hot mask for `SBWTDIO`

Constraints:

- tuple length must be exactly `7`
- `clock_mask` and `data_mask` must be non-zero
- power control is not part of the native tuple; Python owns target power switching

The canonical tuple definition lives in [sbw_config.py](D:/Github/pi-pico-sbw/mpy/sbw_config.py).

## Exported Constants

These globals are exported by the native module:

- `SYS_CLK_HZ`

This is an informational convenience constant for Python. It does not change runtime behavior.

## Exported Functions

### `read_id(hw) -> (ok, jtag_id)`

- `hw`: 7-item hardware tuple
- returns:
  - `ok`: `bool`
  - `jtag_id`: `int`

Performs SBW session bring-up and reads the JTAG ID.

### `bypass_test(hw) -> (ok, captured)`

- `hw`: 7-item hardware tuple
- returns:
  - `ok`: `bool`
  - `captured`: `int`

Runs the BYPASS smoke test using the fixed native pattern.

### `sync_and_por(hw) -> (ok, control_capture)`

- `hw`: 7-item hardware tuple
- returns:
  - `ok`: `bool`
  - `control_capture`: `int`

Synchronizes JTAG, executes the POR flow, and returns the captured control signal word.

### `read_mem16(hw, address) -> (ok, value)`

- `hw`: 7-item hardware tuple
- `address`: target address
- returns:
  - `ok`: `bool`
  - `value`: `int`

Reads a single 16-bit word using the non-quick path.

### `write_mem16(hw, address, value) -> (ok, readback)`

- `hw`: 7-item hardware tuple
- `address`: target address
- `value`: 16-bit word
- returns:
  - `ok`: `bool`
  - `readback`: `int`

Writes a single 16-bit word, reads it back, and returns the readback value.

### `read_block16(hw, address, words) -> (ok, data_bytes)`

- `hw`: 7-item hardware tuple
- `address`: start address
- `words`: number of 16-bit words
- returns:
  - `ok`: `bool`
  - `data_bytes`: `bytes`

This is the native quick block-read API. It returns raw little-endian bytes.

### `write_block16(hw, address, data_bytes) -> bool`

- `hw`: 7-item hardware tuple
- `address`: start address
- `data_bytes`: even-length bytes-like object containing little-endian 16-bit words
- returns:
  - `bool`

This is the native block-write API.

Behavior:

- for FRAM address ranges, it uses the fast quick-write path
- for non-FRAM ranges such as RAM/peripherals, it falls back to the per-word write path

Public contract:

- the block must not cross a protection-class boundary
- a single block must stay entirely within one of:
  - RAM/peripheral
  - info FRAM
  - main FRAM
- `data_bytes` length must be even

### `pio_test_word(hw, pattern_word, fifo_words) -> bool`

- `hw`: 7-item hardware tuple
- `pattern_word`: packed chunk descriptor
  - bits `[4:0]` = logical bit count minus 1
  - bits above that = successive `(TMS, TDI)` pairs, LSB-first
- `fifo_words`: number of times to repeat that packed FIFO word
- returns:
  - `bool`

This is a standalone PIO waveform exerciser for scope/debug work. It does not
perform SBW entry, JTAG state handling, or target memory operations. It only:

- hands `GP2/GP3` to `PIO0`
- emits repeated logical SBW bit cells at the fixed `10 MHz` slot rate
- restores the pins to normal SIO ownership afterward

### `pio_packet_words(hw, data_bytes) -> bool`

- `hw`: 7-item hardware tuple
- `data_bytes`: bytes-like object containing little-endian 32-bit FIFO words
- returns:
  - `bool`

This is the packet-driven SBW PIO test harness. Each 32-bit FIFO word packs up
to 8 logical packets in 4-bit nibbles, LSB-first:

- bit 0: `DONE`
- bit 1: `TMS`
- bit 2: `TDI1`
- bit 3: `TDI2` / held level after the TDO slot

The packet engine:

- uses `TDI1 == TDI2` for ordinary driven bits
- allows `TDI1 != TDI2` to model held-level transitions
- releases `SBWTDIO` only during the TDO slot
- treats `DONE=1` as “no more packets in this FIFO word”
- flushes the current ISR contents to RX FIFO
- discards the remaining bits of the current FIFO word and resumes from the next word

### `pio_clock_square(hw, duration_us) -> bool`

- `hw`: 7-item hardware tuple
- `duration_us`: approximate burst length in microseconds
- returns:
  - `bool`

This is the simplest possible PIO smoke test:

- only `SBWTCK` is handed to `PIO0`
- `SBWTDIO` stays out of the test
- the PIO program is just two wrapped instructions that generate a `10 MHz`
  square wave on the clock pin

### `pio_clock_data_square(hw, duration_us) -> bool`

- `hw`: 7-item hardware tuple
- `duration_us`: approximate burst length in microseconds
- returns:
  - `bool`

This is the next-step two-pin smoke test:

- `GP2` and `GP3` are both handed to `PIO0`
- the PIO program is just two wrapped `SET PINS` instructions
- `GP2` and `GP3` are driven as opposite-phase `10 MHz` square waves

## Ownership Boundary

The native module owns:

- timing-critical SBW slot generation
- JTAG sequencing
- target memory read/write implementation
- FRAM quick-write and quick-read mechanics

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

The caller must enforce this before calling the native API. The current enforcement lives in [sbw_config.py](D:/Github/pi-pico-sbw/mpy/sbw_config.py) and is called by [sbw.py](D:/Github/pi-pico-sbw/mpy/sbw.py).

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
