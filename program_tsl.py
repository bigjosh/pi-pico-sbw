import sys
import time

import select

from sbw import SBW
from target_power import TargetPower
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

FIRMWARE_FILE_NAME = "tsl-calibre-msp.txt"
DEVICE_DESCRIPTOR_ADDRESS = 0x1A00
DEVICE_DESCRIPTOR_LENGTH = 0x12
TIMESTAMP_ADDRESS = 0x1800
POST_PROGRAM_RUN_MS = 1000
REBOOT_OFF_MS = 50


def parse_titxt_blocks(titxt_data):
    blocks = []
    current_start = None
    current_data = bytearray()

    for raw_line in titxt_data.decode("ascii").splitlines():
        line = raw_line.strip()
        if not line:
            continue
        if line[0] in {"q", "Q"}:
            break
        if line.startswith("@"):
            if current_start is not None and current_data:
                blocks.append((current_start, bytes(current_data)))
                current_data = bytearray()
            current_start = int(line[1:], 16)
            continue

        if current_start is None:
            raise ValueError("TI-TXT data line appeared before an address header")

        current_data.extend(bytes.fromhex(line))

    if current_start is not None and current_data:
        blocks.append((current_start, bytes(current_data)))

    return blocks


def merge_contiguous_blocks(blocks):
    merged = []

    for address, data in blocks:
        if not merged:
            merged.append((address, bytes(data)))
            continue

        prev_address, prev_data = merged[-1]
        prev_end = prev_address + len(prev_data)

        if address == prev_end:
            merged[-1] = (prev_address, prev_data + data)
        else:
            merged.append((address, bytes(data)))

    return merged


def build_timestamp_bytes(now):
    if isinstance(now, tuple):
        year, month, mday, hour, minute, second, weekday = now[:7]
    else:
        second = now.tm_sec
        minute = now.tm_min
        hour = now.tm_hour
        weekday = now.tm_wday
        mday = now.tm_mday
        month = now.tm_mon
        year = now.tm_year

    return bytes(
        [
            second,
            minute,
            hour,
            weekday,
            mday,
            month,
            year % 100,
        ]
    )


def get_device_uuid(dd_bytes):
    device_info = dd_bytes[0x04:0x08]
    lot_wafer = dd_bytes[0x0A:0x0E]
    die_x_pos = dd_bytes[0x0E:0x10]
    die_y_pos = dd_bytes[0x10:0x12]
    return "".join("%02X" % value for value in device_info + lot_wafer + die_x_pos + die_y_pos)


def load_firmware_blocks(path=FIRMWARE_FILE_NAME):
    print("Loading %s into memory..." % path)
    with open(path, "rb") as handle:
        titxt_data = handle.read()

    raw_blocks = parse_titxt_blocks(titxt_data)
    blocks = merge_contiguous_blocks(raw_blocks)
    total_bytes = sum(len(data) for _, data in blocks)

    print(
        "Firmware is %d block(s), merged to %d write block(s), %d bytes.\n"
        % (len(raw_blocks), len(blocks), total_bytes)
    )
    return blocks


def program_once(power, sbw, firmware_blocks):
    t0 = time.ticks_ms()

    def tp(msg):
        dt = time.ticks_diff(time.ticks_ms(), t0)
        print("%02d.%03d %s" % (dt // 1000, dt % 1000, msg))

    now = time.gmtime()

    tp("Powering on target...")
    power.on()
    try:
        tp("Reading JTAG ID...")
        ok, jtag_id = sbw.read_id()
        if not ok or jtag_id != sbw_native.JTAG_ID_EXPECTED:
            raise RuntimeError("expected JTAG ID 0x%02X, found 0x%02X" % (sbw_native.JTAG_ID_EXPECTED, jtag_id))

        tp("Reading device descriptor...")
        ok, dd_bytes = sbw.read_bytes(DEVICE_DESCRIPTOR_ADDRESS, DEVICE_DESCRIPTOR_LENGTH)
        if not ok or len(dd_bytes) != DEVICE_DESCRIPTOR_LENGTH:
            raise RuntimeError("failed to read device descriptor")

        device_uuid = get_device_uuid(dd_bytes)
        tp("Device UUID is %s" % device_uuid)

        # Write all blocks
        timestamp_data = build_timestamp_bytes(now)
        tp("Writing commissioning timestamp...")
        if not sbw.write_bytes(TIMESTAMP_ADDRESS, timestamp_data):
            raise RuntimeError("write failed for timestamp at 0x%05X" % TIMESTAMP_ADDRESS)

        for index, (address, data) in enumerate(firmware_blocks, start=1):
            tp("Writing firmware block %d/%d addr=0x%05X len=%d..." % (
                index, len(firmware_blocks), address & 0xFFFFF, len(data)))
            if not sbw.write_bytes(address, data):
                raise RuntimeError("write failed at 0x%05X" % (address & 0xFFFFF))

        # Verify all blocks
        tp("Verifying commissioning timestamp...")
        ok, _ = sbw.verify_bytes(TIMESTAMP_ADDRESS, timestamp_data)
        if not ok:
            raise RuntimeError("verify failed for timestamp at 0x%05X" % TIMESTAMP_ADDRESS)

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

    # Power-cycle target to run briefly
    tp("Starting target execution...")
    power.off()
    time.sleep_ms(REBOOT_OFF_MS)
    power.on()
    try:
        tp("Waiting %g seconds before disconnect..." % (POST_PROGRAM_RUN_MS / 1000.0))
        time.sleep_ms(POST_PROGRAM_RUN_MS)
        tp("Disconnect the programming connector to remove power from the TSL.")
    finally:
        power.off()

    tp("Done.")
    return device_uuid


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
    print("Logging disabled.")
    firmware_blocks = load_firmware_blocks()
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
