# WS2812B Pixel Driver

Low-level driver for WS2812B LED strips using a PIO state machine.
No dependency on MicroPython's built-in `neopixel` module.

## Hardware

| Pico Pin | GPIO | Function |
|----------|------|----------|
| 33 | GND | Strip GND |
| 34 | GP28 | Strip data in |
| 36 | 3V3 | Strip VCC |

Powered at 3.3V from the Pico's regulator. Colors are dimmer than at 5V
but logic levels are guaranteed. Keep brightness moderate to stay within
the regulator's current budget (~20mA per pixel at full white).

## Usage

```python
from pixels import Pixels, RED, GREEN, BLUE, BLACK, rgb

px = Pixels(28)                           # GP28, PIO state machine 0
px.show([RED, GREEN, BLUE, BLACK, BLACK]) # set 5 pixels
px.show([rgb(30, 0, 0)] * 5)             # all dim red
px.show([BLACK] * 5)                      # all off
```

## API

### `Pixels(pin, sm_id=0)`

Initialize a PIO state machine for WS2812B output on the given GPIO pin.
`sm_id` selects which PIO state machine to use (0-11 on RP2350). We do this
in case you want to use other SMs for other things so you can control which
one goes where. 

### `show(words)`

Write pixel data to the strip. `words` is a list or tuple of 32-bit
values, one per pixel. Each value encodes GRB color data in the bottom
24 bits (top 8 bits ignored). Use `rgb()` or the color constants to
create these values.

The strip length is determined by the length of `words` — there is no
fixed pixel count.

### `rgb(r, g, b)`

Pack 8-bit red, green, blue values into a 32-bit GRB word suitable for
`show()`. Values 0-255 per channel.

### Color Constants

16 pre-defined saturated colors:

| Constant | RGB |
|----------|-----|
| `BLACK` | (0, 0, 0) |
| `RED` | (255, 0, 0) |
| `GREEN` | (0, 255, 0) |
| `BLUE` | (0, 0, 255) |
| `CYAN` | (0, 255, 255) |
| `MAGENTA` | (255, 0, 255) |
| `YELLOW` | (255, 255, 0) |
| `WHITE` | (255, 255, 255) |
| `ORANGE` | (255, 128, 0) |
| `PURPLE` | (128, 0, 255) |
| `PINK` | (255, 0, 128) |
| `LIME` | (128, 255, 0) |
| `AQUA` | (0, 128, 255) |
| `NAVY` | (0, 0, 128) |
| `MAROON` | (128, 0, 0) |
| `OLIVE` | (128, 128, 0) |

For factory use, scale brightness down with `rgb()`:
```python
DIM_RED = rgb(30, 0, 0)
DIM_GREEN = rgb(0, 30, 0)
```

## How It Works

The PIO state machine runs a standard WS2812B bit-banging program at
8 MHz (10 cycles per bit = 800 kHz, matching the WS2812B protocol).

Each bit is encoded as:
- **1-bit**: high for 7 cycles (875ns), low for 3 cycles (375ns)
- **0-bit**: high for 3 cycles (375ns), low for 7 cycles (875ns)

The PIO program uses autopull with a 24-bit threshold. `show()` writes
each 32-bit word to the FIFO with a 24-bit left shift so the GRB data
occupies the top 24 bits that the PIO shifts out.

A >50µs gap between `show()` calls acts as the reset signal that latches
the data into the strip.

## PIO Program

```
.wrap_target
bitloop:
    out x, 1       side 0 [2]  ; shift 1 bit, low for 3 cycles
    jmp !x do_zero side 1 [1]  ; high for 2 cycles, branch on bit value
    jmp bitloop    side 1 [4]  ; bit=1: stay high 5 more (total 7 high)
do_zero:
    nop            side 0 [4]  ; bit=0: go low 5 more (total 7 low)
.wrap
```

4 instructions, fits easily in any PIO program slot.
