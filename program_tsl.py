"""TSL factory programmer with auto-detect and Google Sheets logging.

Autodetects target connection, programs firmware + commissioning timestamp,
verifies, power-cycles the target to run briefly, then logs the result to
a Google Sheet. Waits for target removal before the next cycle.

Requires secrets.py with WIFI_SSID, WIFI_PASSWORD, and SHEETS_URL.

Pin connections below.
"""
import time

from sbw import SBW
from utils import get_device_uuid
import sbw_native
from pixels import *

# Pico Pin | GPIO | Function
# ---------|------|----------
#       29 | GP22 | SBWTDIO
#       31 | GP26 | SBWTCK (must have ADC)
#       32 | GP27 | VCC
SBW_PIN_CLOCK = 26
SBW_PIN_DATA = 22
SBW_PIN_POWER = 27
POWER_SETTLE_MS = 20
PIXEL_PIN = 28

FIRMWARE_FILE_NAME = "tsl-calibre-msp.txt"

# from part datasheets
DEVICE_DESCRIPTOR_ADDRESS = 0x1A00
DEVICE_DESCRIPTOR_LENGTH = 0x12

# beging of USER FRAM
TIMESTAMP_ADDRESS = 0x1800

# based on part and circuit and firmware
REBOOT_OFF_MS = 100
BOOT_SETTLE_MS = 5000

# emperical acceptable current range for a TSL
LPM_CURRENT_MIN_UA = 1
LPM_CURRENT_MAX_UA = 3

# Pixel indices for headless status strip.
READY   = 0
STATE   = 1

# progress
PROGRAM = 3
BOOT    = 4
CURRENT = 5
WIFI    = 6
NTP     = 7
LOG     = 8
# IMPORTANT: Update StatusPixels count below if you add/remove steps.

# Semantic pixel colors
C_READY   = LT_GREEN
C_PENDING = LT_YELLOW
C_PASS    = LT_GREEN
C_FAIL    = RED


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
    import machine
    import network
    from secrets import WIFI_SSID, WIFI_PASSWORD, SHEETS_URL
    from target_power import TargetPower, TargetPowerWithDetect
    from utils import load_firmware_blocks, firmware_hash
    from addrow import add_row, flush_once, pending_count
    import wifi

    firmware_blocks = load_firmware_blocks(FIRMWARE_FILE_NAME)

    fw_hash = firmware_hash(firmware_blocks)
    mac_addr = "".join("%02X" % b for b in network.WLAN(network.STA_IF).config("mac"))

    r_pullup = TargetPower.load_calibration()
    print("Pull-up calibration: %.0f ohm" % r_pullup)

    power = TargetPowerWithDetect(SBW_PIN_POWER, SBW_PIN_CLOCK, POWER_SETTLE_MS)
    sbw = SBW(SBW_PIN_CLOCK, SBW_PIN_DATA)
    led = machine.Pin("LED", machine.Pin.OUT, value=0)

    sp = StatusPixels(PIXEL_PIN, 9)     # I hate this hardcode, but I can not figure out pythonic way to do it better. 
                                        # IMPORTANT: Update StatusPixels count if you add/remove steps.

    while True:
        sp.clear()

        print("Waiting for target...")
        sp.set(READY, C_READY)
        power.wait_for_connect()

        print("Target detected.")
        sp.set(READY, BLACK)
        sp.set(PROGRAM, C_PENDING)

        device_uuid = None
        status = "fail"
        current_ua = None

        print("Powering on target...")
        power.on()

        led.on()
        try:
            device_uuid = program_once(power, sbw, firmware_blocks)
            status = "programmed"
            sp.set(PROGRAM, C_PASS)
        except Exception as exc:
            print("Programming failed: %s" % exc)
            sp.set(PROGRAM, C_FAIL)
        finally:
            led.off()

        # Reboot target so newly programmed firmware starts running
        print("Rebooting target...")
        sp.set(BOOT, C_PENDING)
        power.off()
        time.sleep_ms(REBOOT_OFF_MS)
        power.on()

        print("Waiting %dms for target to start up..." % BOOT_SETTLE_MS)
        time.sleep_ms(BOOT_SETTLE_MS)
        sp.set(BOOT, C_PASS)

        # Measure LPM current to test for defects        
        sp.set(CURRENT, C_PENDING)
        print("Switching target power to internal pull-up for current measurement...")
        power.power_via_pullup()
        time.sleep_ms(200)
        current_ua = power.measure_current_ua(r_pullup, 200)
        if current_ua is None:
            status = "brownout"
            print("Brownout — target not in LPM.")
            sp.set(CURRENT, C_FAIL)
        else:
            if (current_ua < LPM_CURRENT_MIN_UA or current_ua > LPM_CURRENT_MAX_UA):
                status = "lpm_fail"
                print("Target current out of LPM spec: %.0f µA" % current_ua)
                sp.set(CURRENT, C_FAIL)
                sp.set(STATE, C_FAIL)
            else:
                status = "pass"
                print("Target current draw: %.0f µA" % current_ua)
                sp.set(CURRENT, C_PASS)
                sp.set(STATE, C_PASS)

        # Queue log row
        now = time.gmtime()
        timestamp = "%04d-%02d-%02d %02d:%02d:%02d" % (now[0], now[1], now[2], now[3], now[4], now[5])
        add_row(SHEETS_URL, [device_uuid or "unknown", timestamp, fw_hash, mac_addr, status, current_ua])

        # WiFi connect + NTP sync
        sp.set(WIFI, C_PENDING)
        while not wifi.is_connected():
            print("WiFi not connected, reconnecting...")
            try:
                wifi.connect(WIFI_SSID, WIFI_PASSWORD)
                print("WiFi connected.")
            except Exception as e:
                print("WiFi error: %s" % e)
                sp.set(WIFI, C_FAIL)
                continue
        sp.set(WIFI, C_PASS)

        sp.set(NTP, C_PENDING)
        import ntptime
        try:
            ntptime.settime()
            sp.set(NTP, C_PASS)
        except Exception:
            sp.set(NTP, C_FAIL)

        # Drain pending log rows
        sp.set(LOG, C_PENDING)
        while pending_count():
            flush_once(SHEETS_URL)
        sp.set(LOG, C_PASS)

        print("Powering off target...")
        sbw.release()
        power.off()

        print("Waiting for target to be removed...")
        power.wait_for_disconnect()
        print("Target removed.\n")

program_loop()
