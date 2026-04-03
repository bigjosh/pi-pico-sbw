"""Simple and fast interactive MSP430 SBW programmer.
Programs the file `program.txt` into target. 
Connect to the Pico over USB serial and open a terminal to it. 
Waits for a keypress to start each cycle.
Pin connections below.
"""
import sys
import time

import select

from sbw import SBW
from target_power import TargetPower
from utils import load_firmware_blocks
import sbw_native

# Pico Pin | GPIO | Target Pin
# ---------|------|----------
#       31 | GP26 | SBWTCK
#       32 | GP27 | SBWTDIO
#       33 | GND  | GND
#       34 | GP28 | VCC
SBW_PIN_CLOCK = 26
SBW_PIN_DATA = 27
SBW_PIN_POWER = 28
POWER_SETTLE_MS = 20

FIRMWARE_FILE_NAME = "program.txt"


def program_once(power, sbw, firmware_blocks):
    t0 = time.ticks_ms()

    def tp(msg):
        """Print with ##.### seconmds timestamp"""
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


_stdin_poll = select.poll()
_stdin_poll.register(sys.stdin, select.POLLIN)


def _drain_stdin():
    while _stdin_poll.poll(0):
        sys.stdin.read(1)


def _wait_key():
    while not _stdin_poll.poll(0):
        time.sleep_ms(10)
    return sys.stdin.read(1)


def program_loop():
    firmware_blocks = load_firmware_blocks(FIRMWARE_FILE_NAME)
    power = TargetPower(SBW_PIN_POWER, POWER_SETTLE_MS)
    sbw = SBW(SBW_PIN_CLOCK, SBW_PIN_DATA)

    while True:
        _drain_stdin()
        print("\rPress [spacebar] to start programming cycle, any other key to exit...")

        key = _wait_key()
        if key != " ":
            print("Exited by user.")
            return

        try:
            program_once(power, sbw, firmware_blocks)
        except Exception as exc:
            print("Programming failed: %s\n" % exc)


if __name__ == "__main__":
    program_loop()
