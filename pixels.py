"""WS2812B pixel driver using PIO.

Drives a WS2812B LED strip directly via a PIO state machine. No
dependency on MicroPython's built-in neopixel module.

THe sm is nortmally disabled, and we fill the fifo with it disabled to make
sure we do not get interrupted in the middle of a string which would reset
the stream. 

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

import _thread



@rp2.asm_pio(sideset_init=rp2.PIO.OUT_LOW, out_shiftdir=rp2.PIO.SHIFT_LEFT,
             autopull=True, pull_thresh=24, fifo_join=rp2.PIO.JOIN_TX)
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
LT_YELLOW = rgb(40, 16, 0)


class Pixels:
    FIFO_DEPTH = 8  # TX + RX joined = 8 entries

    def __init__(self, pin, sm_id=0):
        """Initialize PIO state machine for WS2812B on the given GPIO pin.

        pin: GPIO number for the data line
        sm_id: PIO state machine index (0-11 on RP2350)
        """
        self._sm = rp2.StateMachine(sm_id, _ws2812,
            freq=8_000_000, sideset_base=machine.Pin(pin))

        # make sure we can call from both threads without garbling display
        self.lock = _thread.allocate_lock()

    def show(self, words):
        """Write pixel data to the WS2812B LED strip.

        Transmits GRB color values to the PIO state machine for the addressable LED strip.

        words: list/tuple of 32-bit GRB values (one per pixel), as returned by
               rgb() or the color constants. Top 8 bits are ignored.

        The second argument (8) to _sm.put() specifies the number of bits to shift out.
        The 1ms sleep allows PIO to finish transmitting and provides the 50µs reset gap
        required by WS2812B protocol to latch the new colors.

        Thread-safe via lock acquisition.
        """
        self.lock.acquire()
        # print("%d core%d pixel locked" % (time.ticks_ms(), machine.mem32[0xD0000000]))
        try:
            for w in words:
                self._sm.put(w, 8)
            self._sm.active(1)
            while self._sm.tx_fifo() > 0:
                time.sleep_ms(1)                # breath
            time.sleep_ms(1)                       # let new pixels latch
            self._sm.active(0)
        finally:
            self.lock.release()
            # print("%d core%d pixel released" % (time.ticks_ms(), machine.mem32[0xD0000000]))


class StatusPixels:
    """Buffered pixel strip that remembers state between updates.

    Each call to set() updates one pixel and refreshes the whole strip.
    """

    def __init__(self, pin, sm_id=0):
        self._px = Pixels(pin, sm_id)
        self._buf = [BLACK] * Pixels.FIFO_DEPTH

    def set(self, index, color):
        """Set one pixel and refresh the strip."""
        self._buf[index] = color
        self._px.show(self._buf)




# FG: set RED done
# BG: set BLUE
# Filled fifo
# SM active
# fifo empty
# BG: set BLUE done
# FG: set GREEN
# Filled fifo
# SM active
# fifo empty
# FG: set GREEN done
# FG: set REDBG: set YELLOW
