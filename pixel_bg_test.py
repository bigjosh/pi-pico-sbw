"""Test pixel updates from both foreground and background threads.

FG: blinks pixel 0 red/green, 1.5s period
BG: blinks pixel 1 blue/yellow, 2s period
"""
import time
import _thread
from pixels import StatusPixels, RED, GREEN, BLUE, YELLOW, BLACK

PIXEL_PIN = 28

sp = StatusPixels(PIXEL_PIN)

def bg_blink():
    while True:
        print("BG: set BLUE")
        sp.set(1, BLUE)
        print("BG: set BLUE done")
        time.sleep_ms(300)
        print("BG: set YELLOW")
        sp.set(1, YELLOW)
        print("BG: set YELLOW done")
        time.sleep_ms(300)

_thread.start_new_thread(bg_blink, ())

while True:
    print("FG: set RED")
    sp.set(0, RED)
    print("FG: set RED done")
    time.sleep_ms(200)
    print("FG: set GREEN")
    sp.set(0, GREEN)
    print("FG: set GREEN done")
    time.sleep_ms(200)
