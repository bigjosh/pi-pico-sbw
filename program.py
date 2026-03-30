import sys
import time

try:
    import uselect as select
except ImportError:
    import select

from sbw import SBWNative
from sbw_config import JTAG_ID_EXPECTED


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


def prepare_firmware_streams(sbw, blocks):
    """Pre-compute DMA TX streams for all firmware blocks.

    Call once at startup. Returns list of (address, stream, data) tuples
    ready for sbw.send_write_block() and sbw.verify_bytes().
    """
    import time
    print("Pre-computing firmware streams...")
    t0 = time.ticks_us()
    prepared = []
    for address, data in blocks:
        stream = sbw.prepare_write_stream(data)
        prepared.append((address, stream, data))
    dt = time.ticks_diff(time.ticks_us(), t0)
    total_bytes = sum(len(d) for _, _, d in prepared)
    print("  %d block(s), %d bytes, prepared in %d ms\n" % (
        len(prepared), total_bytes, dt // 1000))
    return prepared


def _find_first_mismatch(expected, actual):
    limit = min(len(expected), len(actual))
    for index in range(limit):
        if expected[index] != actual[index]:
            return index
    if len(expected) != len(actual):
        return limit
    return None


def _write_and_verify_bytes(sbw, label, address, data):
    print("Writing %s..." % label)
    if not sbw.write_bytes(address, data):
        raise RuntimeError("write failed for %s at 0x%05X" % (label, address & 0xFFFFF))

    print("Verifying %s..." % label)
    ok, actual = sbw.verify_bytes(address, data)
    if ok:
        return

    mismatch = _find_first_mismatch(data, actual)
    if mismatch is None:
        raise RuntimeError("verify failed for %s at 0x%05X" % (label, address & 0xFFFFF))

    raise RuntimeError("verify failed for %s at 0x%05X" % (label, (address + mismatch) & 0xFFFFF))


def _run_target_for_observation(sbw):
    print("Starting target execution...")
    sbw.power_off()
    time.sleep_ms(REBOOT_OFF_MS)
    sbw.power_on()
    try:
        print("Waiting %g seconds before disconnect..." % (POST_PROGRAM_RUN_MS / 1000.0))
        time.sleep_ms(POST_PROGRAM_RUN_MS)
        print("Disconnect the programming connector to remove power from the TSL.")
    finally:
        sbw.power_off()


def program_once(sbw, prepared_blocks, now=None):
    """Program the target using pre-computed firmware streams.

    prepared_blocks: list from prepare_firmware_streams().
    """
    if now is None:
        now = time.gmtime()

    sbw.power_on()
    try:
        ok, jtag_id = sbw.read_id()
        if not ok or jtag_id != JTAG_ID_EXPECTED:
            raise RuntimeError("expected JTAG ID 0x%02X, found 0x%02X" % (JTAG_ID_EXPECTED, jtag_id))

        print("Reading device descriptor...")
        ok, dd_bytes = sbw.read_bytes(DEVICE_DESCRIPTOR_ADDRESS, DEVICE_DESCRIPTOR_LENGTH)
        if not ok or len(dd_bytes) != DEVICE_DESCRIPTOR_LENGTH:
            raise RuntimeError("failed to read device descriptor")

        device_uuid = get_device_uuid(dd_bytes)
        print("Device UUID is %s" % device_uuid)

        _write_and_verify_bytes(sbw, "commissioning timestamp", TIMESTAMP_ADDRESS, build_timestamp_bytes(now))

        for index, (address, stream, data) in enumerate(prepared_blocks, start=1):
            label = "firmware block %d/%d addr=0x%05X len=%d" % (
                index, len(prepared_blocks), address & 0xFFFFF, len(data))

            print("Writing %s..." % label)
            if not sbw.send_write_block(address, stream):
                raise RuntimeError("write failed for %s" % label)

            print("Verifying %s..." % label)
            ok, actual = sbw.verify_bytes(address, data)
            if not ok:
                mismatch = _find_first_mismatch(data, actual)
                raise RuntimeError("verify failed for %s at 0x%05X" % (
                    label, (address + (mismatch or 0)) & 0xFFFFF))
    except Exception:
        sbw.power_off()
        raise

    _run_target_for_observation(sbw)
    return device_uuid


class _SerialConsole:
    def __init__(self):
        self._poll = select.poll()
        self._poll.register(sys.stdin, select.POLLIN)

    def key_available(self):
        return bool(self._poll.poll(0))

    def getch(self):
        if not self.key_available():
            return None
        value = sys.stdin.read(1)
        if isinstance(value, bytes):
            value = value.decode("utf-8", "ignore")
        return value or None

    def drain(self):
        while self.key_available():
            value = self.getch()
            if value is None:
                break


def program_loop():
    print("Logging disabled.")
    firmware_blocks = load_firmware_blocks()
    sbw = SBWNative()
    prepared_blocks = prepare_firmware_streams(sbw, firmware_blocks)
    console = _SerialConsole()

    while True:
        console.drain()
        print("\rPress [spacebar] to start programming cycle, any other key to exit...")

        key = None
        while key is None:
            key = console.getch()
            if key is None:
                time.sleep_ms(10)

        if key != " ":
            print("Exited by user, key = %s" % key)
            return

        print("Programming cycle started.")

        try:
            program_once(sbw, prepared_blocks)
            print("Programming cycle finished.\n")
        except Exception as exc:
            print("Programming failed: %s\n" % exc)


if __name__ == "__main__":
    program_loop()
