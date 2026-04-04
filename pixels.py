"""WS2812B pixel driver using PIO.

Drives a WS2812B LED strip directly via a PIO state machine. No
dependency on MicroPython's built-in neopixel module.

Usage:
    from pixels import *
    px = Pixels(28)                          # GP28, state machine 0
    px.show([RED, GREEN, BLUE, BLACK, BLACK]) # 5 pixels
    px.show([rgb(255, 128, 0)] * 5)          # all orange

    sp = StatusPixels(28, 5)                 # 5-pixel status strip
    sp.set(0, GREEN)                         # set pixel 0 to green
    sp.set(2, RED)                           # set pixel 2 to red
    sp.clear()                               # all off
"""
import time
import rp2
import machine


@rp2.asm_pio(sideset_init=rp2.PIO.OUT_LOW, out_shiftdir=rp2.PIO.SHIFT_LEFT,
             autopull=True, pull_thresh=24)
def _ws2812():
    wrap_target()
    label("bitloop")
    out(x, 1)               .side(0) [2]
    jmp(not_x, "do_zero")   .side(1) [1]
    jmp("bitloop")           .side(1) [4]
    label("do_zero")
    nop()                    .side(0) [4]
    wrap()


def rgb(r, g, b):
    """Pack 8-bit RGB into a 32-bit GRB word for WS2812B."""
    return (g << 16) | (r << 8) | b


# 16 common saturated colors
BLACK   = rgb(0, 0, 0)
RED     = rgb(255, 0, 0)
GREEN   = rgb(0, 255, 0)
BLUE    = rgb(0, 0, 255)
CYAN    = rgb(0, 255, 255)
MAGENTA = rgb(255, 0, 255)
YELLOW  = rgb(255, 255, 0)
WHITE   = rgb(255, 255, 255)
ORANGE  = rgb(255, 128, 0)
PURPLE  = rgb(128, 0, 255)
PINK    = rgb(255, 0, 128)
LIME    = rgb(128, 255, 0)
AQUA    = rgb(0, 128, 255)
NAVY    = rgb(0, 0, 128)
MAROON  = rgb(128, 0, 0)
OLIVE   = rgb(128, 128, 0)

LT_WHITE  = rgb(64, 64, 64)
LT_RED    = rgb(64, 0, 0)
LT_GREEN  = rgb(0, 64, 0)
LT_BLUE   = rgb(0, 0, 64)
LT_YELLOW = rgb(64, 64, 0)


class Pixels:
    def __init__(self, pin, sm_id=0):
        """Initialize PIO state machine for WS2812B on the given GPIO pin.

        pin: GPIO number for the data line
        sm_id: PIO state machine index (0-11 on RP2350)
        """
        self._sm = rp2.StateMachine(sm_id, _ws2812,
            freq=8_000_000, sideset_base=machine.Pin(pin))
        self._sm.active(1)

    def show(self, words):
        """Write pixel data to the strip.

        words: list/tuple of 32-bit GRB values (one per pixel), as
        returned by rgb() or the color constants. Top 8 bits are ignored.
        """
        for w in words:
            self._sm.put(w, 8)
        time.sleep_ms(1)  # wait for PIO to drain + 50µs reset gap to latch


class StatusPixels:
    """Buffered pixel strip that remembers state between updates.

    Each call to set() updates one pixel and refreshes the whole strip.
    """

    def __init__(self, pin, n, sm_id=0):
        self._px = Pixels(pin, sm_id)
        self._buf = [BLACK] * n

    def set(self, index, color):
        """Set one pixel and refresh the strip."""
        self._buf[index] = color
        self._px.show(self._buf)

    def clear(self):
        """Turn all pixels off."""
        self._buf = [BLACK] * len(self._buf)
        self._px.show(self._buf)
