"""TSL factory programmer with auto-detect and Google Sheets logging.

Autodetects target connection, programs firmware + commissioning timestamp,
verifies, power-cycles the target to run briefly, then logs the result to
a Google Sheet. Waits for target removal before the next cycle.

Requires secrets.py with WIFI_SSID, WIFI_PASSWORD, and SHEETS_URL.

Pin connections below.
"""
import time

import machine

from sbw import SBW
from target_power import TargetPowerWithDetect
from utils import load_firmware_blocks, get_device_uuid
from addrow import add_row, flush_once, pending_count
import wifi
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

# Below are the steps in the cycle, followed by the pixel number

POWER   =1
TARGET  =2
PROGRAM =3
WIFI    =4
LOG     =5


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


def program_once(power, sbw, firmware_blocks):
    """Program and verify one target. Returns device UUID on success."""
    t0 = time.ticks_ms()

    def tp(msg):
        """Print with ##.### seconds timestamp."""
        dt = time.ticks_diff(time.ticks_ms(), t0)
        print("%02d.%03d %s" % (dt // 1000, dt % 1000, msg))

    now = time.gmtime()

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

    # Write timestamp + firmware
    timestamp_data = build_timestamp_bytes(now)
    tp("Writing commissioning timestamp...")
    if not sbw.write_bytes(TIMESTAMP_ADDRESS, timestamp_data):
        raise RuntimeError("write failed for timestamp at 0x%05X" % TIMESTAMP_ADDRESS)

    for index, (address, data) in enumerate(firmware_blocks, start=1):
        tp("Writing firmware block %d/%d addr=0x%05X len=%d..." % (
            index, len(firmware_blocks), address & 0xFFFFF, len(data)))
        if not sbw.write_bytes(address, data):
            raise RuntimeError("write failed at 0x%05X" % (address & 0xFFFFF))

    # Verify all
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

    return device_uuid


def program_loop():
    from secrets import WIFI_SSID, WIFI_PASSWORD, SHEETS_URL

    firmware_blocks = load_firmware_blocks(FIRMWARE_FILE_NAME)
    power = TargetPowerWithDetect(SBW_PIN_POWER, SBW_PIN_CLOCK, POWER_SETTLE_MS)
    sbw = SBW(SBW_PIN_CLOCK, SBW_PIN_DATA)
    led = machine.Pin("LED", machine.Pin.OUT, value=0)

    while True:
        print("Waiting for target...")
        power.wait_for_connect()
        print("Target detected.")

        device_uuid = None
        status = "fail"

        print("Powering on target...")
        power.on()

        led.on()
        try:
            device_uuid = program_once(power, sbw, firmware_blocks)
            status = "pass"
        except Exception as exc:
            print("Programming failed: %s" % exc)
        finally:
            led.off()

        # Queue log row (non-blocking)
        print("Queueing log row...")
        now = time.gmtime()
        timestamp = "%04d-%02d-%02d %02d:%02d:%02d" % (now[0], now[1], now[2], now[3], now[4], now[5])
        add_row(SHEETS_URL, [device_uuid or "unknown", timestamp, status])

        # drain pending requests 
        while pending_count():

            # ensure wifi is connected before attempting to flush pending rows
            while not wifi.is_connected():
                print("WiFi not connected, reconnecting...")
                try:
                    wifi.connect(WIFI_SSID, WIFI_PASSWORD)
                    print("WiFi connected.")
                except Exception as e:
                    print("WiFi error: %s" % e)
                    continue
            print("Transmitting pending row...")
            flush_once(SHEETS_URL)

        print("Powering off target...")
        power.off()

        print("Waiting for target to be removed...")
        power.wait_for_disconnect()
        print("Target removed.\n")


program_loop()
