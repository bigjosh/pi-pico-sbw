"""TSL factory programmer with auto-detect and Google Sheets logging.

Autodetects target connection, programs firmware + commissioning timestamp,
verifies, power-cycles the target, measures current usage, then logs the result to
a Google Sheet. Waits for target removal before the next cycle.

Requires secrets.py with WIFI_SSID, WIFI_PASSWORD, and SHEETS_URL.

Pin connections below.
"""
import time
import _thread
import safe_time
from sbw import SBW
from secrets import WIFI_SSID, WIFI_PASSWORD, WIFI_COUNTRY, SHEETS_URL, FIRMWARE_URL      
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
SBW_PIN_RES   = 21         # 100K ohm resistor to SBW_PIN_POWER
POWER_SETTLE_MS = 20
PIXEL_PIN = 28

FIRMWARE_FILE_NAME = "tsl-calibre-msp.txt"

# from part datasheets
DEVICE_DESCRIPTOR_ADDRESS = 0x1A00
DEVICE_DESCRIPTOR_LENGTH = 0x12

# beging of USER FRAM
TIMESTAMP_ADDRESS = 0x1800
INIT_FLAG_ADDRESS = 0x180E  # initalized_flag in persistent_data_t

# based on part and circuit and firmware
REBOOT_OFF_MS  = 100
# This is dominated by the 0.5s the TSL must wait for the RV3032 to stabilize after power-up.
# This param is tSTART and speced 0.1s typical, 0.5s max. 
BOOT_SETTLE_MS = 700

# emperical acceptable current range for a TSL in "tEStIng OnLy" mode. 
LPM_CURRENT_MIN_UA = 0.5
LPM_CURRENT_MAX_UA = 3

# External burden resistor for current measurement
R_EXT = 100_000  # 100k ohm between SBW_PIN_RES and SBW_PIN_POWER
                 # This has to be big enough to reliabily see 0.5 min current, but small enough that we don't brown out the MCU at the higher end that we want to read. 
VCC = 3.32       # measured supply voltage
# Emperically measured time for target voltage to settle after switching to burden resistor
CURRENT_SETTLE_MS = 500
# How long to sample power for. Longer is better, but we don't want to make operator wait too long.
CURRENT_SAMPLE_MS = 100

# WiFi timeouts
SNTP_RETRY_INTERVAL_S   = 60        # Retry on failure every 60 seconds
SNTP_REFRESH_INTERVAL_S = 60*60*24  # refresh SNTP time once a day

# Pixel indices for headless status strip.
READY   = 0
STATE   = 1

# progress
PROGRAM     = 2
CURRENT     = 3
INIT        = 4

# Startup status steps
WIFI        = 5
NTP         = 6
LOG         = 7
# Pixel indices must be < Pixels.FIFO_DEPTH (8) to fit in the PIO FIFO.

# Semantic pixel colors
C_READY   = GREEN
C_PENDING = LT_YELLOW
C_PASS    = LT_GREEN
C_FAIL    = RED
C_ACTIVE  = YELLOW      # currently active / in-progress


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


def set_init_flag_to(sbw, value):
    """Write a 16-bit value to the init flag address and verify via readback."""
    sbw.connect()
    ok, jtag_id = sbw.read_id()
    if not ok:
        raise RuntimeError("Failed to enter JTAG")
    ok, _ = sbw.write_mem16(INIT_FLAG_ADDRESS, value)
    if not ok:
        raise RuntimeError("Failed to write init flag")
    ok, readback = sbw.read_mem16(INIT_FLAG_ADDRESS)
    if not ok or readback != value:
        raise RuntimeError("Verify failed (wrote=0x%04X readback=0x%04X)" % (value, readback))


def program_once(power, sbw, firmware_blocks):
    """Program and verify one target. Returns device UUID on success."""
    t0 = time.ticks_ms()

    def tp(msg):
        """Print with ##.### seconds timestamp."""
        dt = time.ticks_diff(time.ticks_ms(), t0)
        print("%02d.%03d %s" % (dt // 1000, dt % 1000, msg))

    now = safe_time.gmtime()

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


import log
from log import log_row, flush_once, pending_count

# Do wifi connection and logging in a background thread so it doesn't block programming
# we share both the pixel display and the log folder with the main thread, so we added semaphore locks
# for those (sp and the log folder)

def bg_thread(sp, w):

    print("Background thread started.")

    import ntptime
    import safe_time

    # did we start up with a backlog?
    if pending_count() > 0:
        print("Backlog detected: %d pending rows" % pending_count())
        sp.set(LOG, C_PENDING)
    else:
        sp.set(LOG, C_PASS)

    next_sntp_refresh = time.time()

    while True:

        # Step 1: Ensure WiFi connected (test bench pattern)
        if not w.has_ip():
            sp.set(WIFI, C_PENDING)
            if not w.reconnect():
                sp.set(WIFI, C_FAIL)
                time.sleep(1)
                continue
        sp.set(WIFI, C_PASS)

        # Step 2: NTP sync if due (non-fatal — don't disconnect on failure)
        if time.time() >= next_sntp_refresh:
            sp.set(NTP, C_PENDING)
            try:
                safe_time.settime(ntptime)
                sp.set(NTP, C_PASS)
            except Exception:
                sp.set(NTP, C_FAIL)
            next_sntp_refresh = time.time() + SNTP_REFRESH_INTERVAL_S

        # Step 3: Flush pending logs (replaces TCP probe as connectivity proof)
        if pending_count() > 0:
            sp.set(LOG, C_PENDING)
            try:
                while flush_once(SHEETS_URL):
                    pass
            except Exception as e:
                print("Flush failed:", e)
                sp.set(LOG, C_FAIL)
                w.disconnect()
                time.sleep(1)
                continue
            sp.set(LOG, C_PASS)

        time.sleep(1)

def program_loop():

    print("Testing status pixels...")
    sp = StatusPixels(PIXEL_PIN)
    for color in [RED, GREEN, BLUE,BLACK]:
        for i in range(Pixels.FIFO_DEPTH):
            sp.set(i, color)
            time.sleep_ms(100)
        time.sleep_ms(500)

    print("Initializing modules...")
    import machine
    import network
    from target_power import TargetPower, TargetPowerWithDetect
    from utils import load_firmware_blocks, firmware_hash

    # Start Wifi and get MAC before starting bg thread — both threads accessing WLAN can deadlock
    from wifi import WiFi
    w = WiFi(WIFI_SSID, WIFI_PASSWORD, WIFI_COUNTRY)
    mac_addr = w.mac_hex()

    # load firmware touches the file system, so load it before starting the background thread
    print("Loading firmware file %s into memory..." % FIRMWARE_FILE_NAME)
    firmware_blocks = load_firmware_blocks(FIRMWARE_FILE_NAME)
    fw_hash = firmware_hash(firmware_blocks)
    print(
        "Firmware is %d block(s), %d bytes.\n"
        % (len(firmware_blocks), sum(len(data) for _, data in firmware_blocks))
    )

    # get log ready for both threads
    log.init()

    print("Starting background thread...")
    _thread.start_new_thread(bg_thread, (sp,w))

    power = TargetPowerWithDetect(SBW_PIN_POWER, SBW_PIN_CLOCK, POWER_SETTLE_MS)
    sbw = SBW(SBW_PIN_CLOCK, SBW_PIN_DATA)
    led = machine.Pin("LED", machine.Pin.OUT, value=0)


    while True:
        print("Waiting for target...")
        sp.set(READY, C_READY)
        power.wait_for_connect()

        print("Target detected.")
        sp.set(READY, BLACK)
        sp.set(PROGRAM, C_PENDING)

        device_uuid = None
        status = "fail"
        i_min = current_ua = i_max = None

        print("Powering on target...")
        power.on()
        sbw.connect()

        led.on()
        try:

            # we have to clear the init flag before starting so in case it was
            # already set and then we fail here it will not be set anymore. 
            print("Clearing init flag...")
            set_init_flag_to(sbw, 0x0000)
            print("Initialized flag cleared.")
            status = "init_cleared"

            print("Starting programming sequence...")
            device_uuid = program_once(power, sbw, firmware_blocks)
            status = "programmed"
            sp.set(PROGRAM, C_PASS)
        except Exception as exc:
            print("Programming failed: %s" % exc)
            sp.set(PROGRAM, C_FAIL)
            sp.set(STATE, C_FAIL)
        finally:
            led.off()

        # Float the SBW clock and data lines so they don't sink current from the target
        print("Floating SBW pins for current measurement...")
        sbw.release()


        # no need to check current if programming failed
        if status == "programmed":
            # Reboot target so newly programmed firmware starts running
            print("Rebooting target...")
            power.off()  # This forces Vcc to gnd so we dont need to wait for the decoupling cap
            time.sleep_ms(REBOOT_OFF_MS)
            power.on()
            # Wait for the RV3032 to stabilize after power-up before attempting to communicate with it
            print("Waiting %dms for target to start up..." % BOOT_SETTLE_MS)
            time.sleep_ms(BOOT_SETTLE_MS)

            # By the time we get to here, the TSL should be at the "TEST ONLY" screen where power usage is representative.
            # Measure LPM current to test for defects.
            # This sequence matches simple-current-test/main.py exactly.

            sp.set(CURRENT, C_PENDING)

            print("Measuring current via external burden resistor...")
            i_min, current_ua, i_max = power.measure_current_ua(
                SBW_PIN_RES, R_EXT, vcc=VCC,
                settle_ms=CURRENT_SETTLE_MS, sample_ms=CURRENT_SAMPLE_MS
            )
            print("Current: min=%.2f avg=%.2f max=%.2f µA" % (i_min, current_ua, i_max))
            if current_ua < LPM_CURRENT_MIN_UA or current_ua > LPM_CURRENT_MAX_UA:
                status = "lpm_fail"
                print("Target current out of LPM spec.")
                sp.set(CURRENT, C_FAIL)
                sp.set(STATE, C_FAIL)
            else:
                sp.set(CURRENT, C_PASS)

                # Set initialized flag in USER FRAM via SBW
                # Target is already powered (GPIO restored by measure_current_ua)
                sp.set(INIT, C_PENDING)
                print("Setting initialized flag at 0x%04X..." % INIT_FLAG_ADDRESS)
                try:
                    set_init_flag_to(sbw, 0x0001)
                    print("Initialized flag set.")
                    sp.set(INIT, C_PASS)
                    sp.set(STATE, C_PASS)
                    status = "pass"
                except Exception as exc:
                    sbw.release()
                    print("Init flag failed: %s" % exc)
                    sp.set(INIT, C_FAIL)
                    sp.set(STATE, C_FAIL)
                    status = "init_set_fail"

        # Power off target
        print("Powering off target and releasing SBW...")
        power.off()

        # Queue log row
        now = safe_time.gmtime()
        timestamp = "%04d-%02d-%02d %02d:%02d:%02d" % (now[0], now[1], now[2], now[3], now[4], now[5])
        current_str = "[%.2f,%.2f,%.2f]" % (i_min, current_ua, i_max) if current_ua is not None else None
        log_row(SHEETS_URL, [device_uuid or "unknown", timestamp, fw_hash, mac_addr, status, current_str])

        sp.set(LOG,C_PENDING)

        print("Waiting for target to be removed...")
        power.wait_for_disconnect()
        print("Target removed.\n")

        #clear progress from previous run
        print("Clearing LED indicators...")
        sp.set(PROGRAM, BLACK)

        sp.set(CURRENT, BLACK)
        sp.set(INIT, BLACK)
        sp.set(STATE, BLACK)


for i in range(10):
    print(f"Starting in {10 - i} seconds...")
    time.sleep(1)
    
program_loop()
