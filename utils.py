"""Shared utilities for MSP430 SBW programming."""
import hashlib


def parse_titxt_blocks(titxt_data):
    """Parse a TI-TXT binary blob into a list of (address, bytes) blocks."""
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



def load_firmware_blocks(path):
    """Load a TI-TXT file and return a list of (address, bytes) blocks."""
    with open(path, "rb") as handle:
        titxt_data = handle.read()

    blocks = parse_titxt_blocks(titxt_data)
    total_bytes = sum(len(data) for _, data in blocks)

    return blocks


def firmware_hash(blocks):
    """Return the MD5 hex digest of firmware block data."""
    h = hashlib.md5()
    for _, data in blocks:
        h.update(data)
    return "".join("%02x" % b for b in h.digest())


def get_device_uuid(dd_bytes):
    """Extract the device UUID string from an MSP430 device descriptor."""
    device_info = dd_bytes[0x04:0x08]
    lot_wafer = dd_bytes[0x0A:0x0E]
    die_x_pos = dd_bytes[0x0E:0x10]
    die_y_pos = dd_bytes[0x10:0x12]
    return "".join("%02X" % value for value in device_info + lot_wafer + die_x_pos + die_y_pos)
