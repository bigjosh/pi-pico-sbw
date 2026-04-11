"""Headless user FRAM clear with auto-detect and pixel status.

Waits for target, clears user FRAM (0x1800-0x19FF) to 0xFF,
waits for removal, repeats. Uses bottom pixel for ready/not-ready
and first pixel for pass/fail.

To run on boot, set main.py to:
    import clear_user_headless
"""
import time
from sbw import SBW
from target_power import TargetPowerWithDetect
from pixels import LT_YELLOW, StatusPixels, GREEN, RED, BLACK, BLUE

SBW_PIN_CLOCK = 26
SBW_PIN_DATA = 22
SBW_PIN_POWER = 27
PIXEL_PIN = 28

USER_FRAM_START = 0x1800
USER_FRAM_END = 0x19FF

# Pixel indices
READY  = 0  # bottom pixel — ready for target
RESULT = 1   # pass/fail

C_READY = GREEN
C_PASS  = BLUE
C_FAIL  = RED
C_PENDING = LT_YELLOW

sp = StatusPixels(PIXEL_PIN)
power = TargetPowerWithDetect(SBW_PIN_POWER, SBW_PIN_CLOCK, settle_ms=100)
sbw = SBW(SBW_PIN_CLOCK, SBW_PIN_DATA)

while True:
    print("Waiting for target...")
    sp.set(READY, C_READY)
    sp.set(RESULT, BLACK)
    power.wait_for_connect()

    print("Target detected.")
    sp.set(READY, BLACK)
    sp.set(RESULT, C_PENDING)
    power.on()
    sbw.connect()

    try:
        # Enter JTAG mode (sync + POR) before any SBW operations
        print("Entering JTAG...")
        ok, jtag_id = sbw.read_id()
        if not ok:
            raise RuntimeError("Failed to enter JTAG (id=0x%02X)" % jtag_id)
        print("JTAG ID: 0x%02X" % jtag_id)

        length = USER_FRAM_END - USER_FRAM_START + 1
        print("Clearing user FRAM 0x%04X-0x%04X (%d bytes)..." % (USER_FRAM_START, USER_FRAM_END, length))
        if not sbw.write_bytes(USER_FRAM_START, b'\xff' * length):
            raise RuntimeError("Write failed")

        ok, readback = sbw.read_bytes(USER_FRAM_START, length)
        if not ok or readback != b'\xff' * length:
            raise RuntimeError("Verify failed")

        print("Cleared and verified.")
        sp.set(RESULT, C_PASS)
    except Exception as exc:
        print("Failed: %s" % exc)
        sp.set(RESULT, C_FAIL)
    finally:
        sbw.release()
        power.off()

    print("Waiting for target to be removed...")
    power.wait_for_disconnect()
    print("Target removed.\n")
