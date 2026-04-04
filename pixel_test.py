"""Cycle through primary colors, lighting each of 8 LEDs in turn."""
import time
from pixels import Pixels, BLACK, RED, GREEN, BLUE, WHITE, YELLOW, CYAN, MAGENTA

px = Pixels(28)
N = 8

for color_name, color in [("RED", RED), ("GREEN", GREEN), ("BLUE", BLUE),
                           ("WHITE", WHITE), ("YELLOW", YELLOW),
                           ("CYAN", CYAN), ("MAGENTA", MAGENTA)]:
    print(color_name)
    for i in range(N):
        buf = [BLACK] * N
        buf[i] = color
        px.show(buf)
        time.sleep_ms(1000)

px.show([BLACK] * N)
print("Done.")
