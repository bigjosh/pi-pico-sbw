"""Fast automatic MSP430 SBW programmer.
Autodetects when a target is connected, programs it automatically, and then waits for
the target to be removed before starting the next cycle. 

Connect to target, wait for LED to go off to know whenb programming is done.

Programs the file `program.txt` into target. 
Pin connections below.
"""
import time

import machine

from sbw import SBW
from target_power import TargetPowerWithDetect
from utils import load_firmware_blocks
import sbw_native

# Pico Pin | GPIO | Function
# ---------|------|----------
#       29 | GP22 | SBWTDIO
#       31 | GP26 | SBWTCK
#       32 | GP27 | VCC
SBW_PIN_CLOCK = 26
SBW_PIN_DATA = 22
SBW_PIN_POWER = 27
POWER_SETTLE_MS = 20

FIRMWARE_FILE_NAME = "program.txt"


def program_once(power, sbw, firmware_blocks):
    t0 = time.ticks_ms()

    def tp(msg):
        """Print with ##.### seconds timestamp"""
        dt = time.ticks_diff(time.ticks_ms(), t0)
        print("%02d.%03d %s" % (dt // 1000, dt % 1000, msg))

    tp("Powering on target...")
    power.on()
    try:
        tp("Reading JTAG ID...")
        ok, jtag_id = sbw.read_id()
        if not ok or jtag_id != sbw_native.JTAG_ID_EXPECTED:
            raise RuntimeError("expected JTAG ID 0x%02X, found 0x%02X" % (sbw_native.JTAG_ID_EXPECTED, jtag_id))

        # Write all blocks
        for index, (address, data) in enumerate(firmware_blocks, start=1):
            tp("Writing firmware block %d/%d addr=0x%05X len=%d..." % (
                index, len(firmware_blocks), address & 0xFFFFF, len(data)))
            if not sbw.write_bytes(address, data):
                raise RuntimeError("write failed at 0x%05X" % (address & 0xFFFFF))

        # Verify all blocks
        tp("Verifying firmware blocks...")
        for index, (address, data) in enumerate(firmware_blocks, start=1):
            tp("Verifying firmware block %d/%d addr=0x%05X len=%d..." % (
                index, len(firmware_blocks), address & 0xFFFFF, len(data)))
            ok, _ = sbw.verify_bytes(address, data)
            if not ok:
                raise RuntimeError("verify failed at 0x%05X" % (address & 0xFFFFF))

        tp("All blocks verified.")
    except Exception:
        power.off()
        raise

    tp("Power off...")
    power.off()

    tp("Done.")


def program_loop():
    firmware_blocks = load_firmware_blocks(FIRMWARE_FILE_NAME)
    power = TargetPowerWithDetect(SBW_PIN_POWER, SBW_PIN_CLOCK, POWER_SETTLE_MS)
    sbw = SBW(SBW_PIN_CLOCK, SBW_PIN_DATA)
    led = machine.Pin("LED", machine.Pin.OUT, value=0)

    while True:
        print("Waiting for target...")
        power.wait_for_connect()
        print("Target detected.")

        led.on()
        try:
            program_once(power, sbw, firmware_blocks)
        except Exception as exc:
            print("Programming failed: %s\n" % exc)
        finally:
            led.off()

        print("Waiting for target to be removed...")
        power.wait_for_disconnect()
        print("Target removed.\n")


if __name__ == "__main__":
    program_loop()
