"""WiFi connection management for Pico W."""
import network
import time

# Don't mess with this stuff. This sequencing seems to work most of the time, others fail - even
# ones copied diretcly from the Pico W examples sometimes fail to connect reliably.
# And this stuff is basically non-documented. :(

# Do note that sometime the Wifi just will not connect on the first try ever. This code
# does seem to always connect on the 2nd try with my Wifi. 


class WiFi:
    def __init__(self, ssid, password, country=None, retry_interval=30):
        self._ssid = ssid
        self._password = password

        if country:
            print("Setting WiFi country to", country)
            network.country(country)

        # station rather than AP
        self._wlan = network.WLAN(network.STA_IF)

        #load network stack
        self._wlan.active(True)

        # By default the wireless chip will active power-saving mode when it is idle, which might lead it to being less responsive. If
        # you are running a server or need more responsiveness, you can change this by toggling the power mode.
        self._wlan.config(pm = 0xa11140) 

        self._next_retry = time.time()
        self.retry_interval = retry_interval

    def is_error(self):
        """Return True if the WiFi is in a stuck error state (LINK_FAIL or BADAUTH)."""
        return self._wlan.status() in (-1, -3)

    def connect(self):
        """Connect to WiFi, returns True on success.

        The CYW43 radio firmware has an intermittent cold-start bug where
        the first association/authentication attempt fails ~50% of the time
        (EV_SET_SSID returns failure status, or WPA2 handshake fails).
        The delay between active() and connect() is irrelevant — the chip
        is ready, it just sometimes fails the handshake.

        Once in LINK_FAIL (-1) or BADAUTH (-3), the driver is stuck —
        it won't self-recover. We detect this, disconnect (which resets
        wifi_join_state via EV_DISASSOC), and retry within the timeout.
        Second attempt almost always succeeds.
        """

        if (self._wlan.status() <= 0 and time.time() >= self._next_retry):
            print("Failed to connect, status:", self._wlan.status())
            print("Disconnecting due to failed connection attempt...")
            # we seem to need this sometimes to reset the stuck state
            self._wlan.disconnect()
            time.sleep(1)
            print("Re-attempting connection...")
            self._wlan.connect(self._ssid, self._password)
            time.sleep(1)
            self._next_retry = time.time() +  self.retry_interval

        return self._wlan.isconnected()
        

    def has_ip(self):
        """Return True if WiFi has an IP address (matches test bench's status check)."""
        return self._wlan.status() == network.STAT_GOT_IP

    def disconnect(self):
        """Disconnect from WiFi."""
        self._wlan.disconnect()

    def reconnect(self):
        """Full disconnect/reconnect cycle with polling. Returns True if connected.

        Mirrors the test bench's connect_wifi() pattern: disconnect, reconnect,
        poll status for up to 20s.
        """
        self._wlan.active(True)
        self._wlan.disconnect()
        print("Connecting to WiFi:", self._ssid)
        self._wlan.connect(self._ssid, self._password)
        for _ in range(20):
            status = self._wlan.status()
            if status == network.STAT_GOT_IP:
                print("WiFi connected:", self._wlan.ipconfig("addr4"))
                return True
            if status < 0:
                print("WiFi failed, status:", status)
                return False
            time.sleep(1)
        print("WiFi connect timed out, status:", self._wlan.status())
        return False

    def is_connected(self):
        """Return True if WiFi is currently connected."""
        return self._wlan.isconnected()

    def mac_hex(self):
        """Return the MAC address as an uppercase hex string."""
        return "".join("%02X" % b for b in self._wlan.config("mac"))

    def ip(self):
        """Return the current IP address string, or None if not connected."""
        if self._wlan.isconnected():
            return self._wlan.ifconfig()[0]
        return None
