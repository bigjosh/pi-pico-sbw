import network
import socket
import time
import _thread
from machine import Pin

SSID = "josh.com"
PASSWORD = "nancy123"
CHECK_INTERVAL = 1

# Shared state: only written by background thread, read by foreground
# 0=down, 1=connecting, 2=probe_failed, 3=connected
state = 0

wlan = network.WLAN(network.STA_IF)


def connect_wifi():
    """Disconnect, reconnect to WiFi, and wait for IP. Returns True if connected."""
    global state
    wlan.active(True)
    wlan.disconnect()
    state = 1
    print("Connecting to WiFi:", SSID)
    wlan.connect(SSID, PASSWORD)

    last_status = None
    for i in range(20):
        status = wlan.status()
        if status != last_status:
            print("  WiFi status:", status)
            last_status = status
        if status == network.STAT_GOT_IP:
            print("WiFi connected:", wlan.ipconfig("addr4"))
            return True
        if status < 0:
            print("WiFi failed, status:", status)
            return False
        time.sleep(1)

    print("WiFi connect timed out after 20s, status:", wlan.status())
    return False


def check_connectivity():
    """TCP connect to 8.8.8.8:53 to verify real network connectivity.
    Raises OSError if no connectivity (expected)."""
    s = socket.socket()
    s.settimeout(5)
    try:
        s.connect(("8.8.8.8", 53))
    finally:
        s.close()


def network_thread():
    """Background thread: manage WiFi and probe connectivity."""
    global state
    while True:
        if wlan.status() != network.STAT_GOT_IP:
            if not connect_wifi():
                state = 0
                time.sleep(CHECK_INTERVAL)
                continue

        try:
            check_connectivity()
        except OSError as e:
            print("No connectivity:", e)
            wlan.disconnect()
            state = 2
            time.sleep(CHECK_INTERVAL)
            continue

        print("Connected")
        state = 3
        time.sleep(CHECK_INTERVAL)


# --- Foreground: LED display ---

led = Pin("LED", Pin.OUT)
led.off()


def blink(n):
    """Blink LED n times (100ms on, 100ms gap each), then 500ms off."""
    for i in range(n):
        led.on()
        time.sleep_ms(100)
        led.off()
        if i < n - 1:
            time.sleep_ms(100)
    time.sleep_ms(500)


print("Starting connectivity test")
_thread.start_new_thread(network_thread, ())

while True:
    s = state
    if s == 0:
        blink(1)
    elif s == 1:
        blink(2)
    elif s == 2:
        blink(3)
    elif s == 3:
        led.on()
        time.sleep_ms(500)
        led.off()
        time.sleep_ms(500)
