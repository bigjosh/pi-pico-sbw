"""WiFi connection management for Pico W."""
import network
import time


def connect(ssid, password, timeout_ms=10000):
    """Connect to a WiFi network. Returns the WLAN interface.

    Raises RuntimeError if the connection times out.
    No-op if already connected.
    """
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    if wlan.isconnected():
        return wlan
    print("Connecting to %s..." % ssid)
    wlan.connect(ssid, password)
    start = time.ticks_ms()
    while not wlan.isconnected():
        if time.ticks_diff(time.ticks_ms(), start) > timeout_ms:
            raise RuntimeError("WiFi connect timed out")
        time.sleep_ms(100)
    print("Connected. IP: %s" % wlan.ifconfig()[0])
    return wlan


def is_connected():
    """Return True if WiFi is currently connected."""
    wlan = network.WLAN(network.STA_IF)
    return wlan.active() and wlan.isconnected()
