import network
import time

from secrets import WIFI_PASSWORD
from secrets import WIFI_SSID
from secrets import FIRMWARE_URL

print("Init wifi...")

network.country("US")
wlan = network.WLAN(network.STA_IF)
wlan.active(True)

# By default the wireless chip will active power-saving mode when it is idle, which might lead it to being less responsive. If
# you are running a server or need more responsiveness, you can change this by toggling the power mode.
wlan.config(pm = 0xa11140)

def _download_firmware(url, max_redirects=5):
    """Download a file from a URL to local storage, following redirects.

    GitHub release URLs redirect through several hops. We follow them
    manually using raw sockets (same pattern as log.py) since
    MicroPython's urequests mishandles POST→GET on redirects and has
    limited redirect support in general.
    """
    import socket, ssl

    # Extract filename from the URL path
    path = url.split("?")[0].split("/")[-1]
    print("Saving to: %s" % path)

    for _ in range(max_redirects + 1):
        proto, _, host_path = url.split("/", 2)
        host_port, rpath = host_path.split("/", 1)
        rpath = "/" + rpath
        use_ssl = proto == "https:"
        port = 443 if use_ssl else 80
        if ":" in host_port:
            host, port = host_port.rsplit(":", 1)
            port = int(port)
        else:
            host = host_port

        hdr = "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n" % (rpath, host)
        s = socket.socket()
        s.connect(socket.getaddrinfo(host, port)[0][-1])
        if use_ssl:
            s = ssl.wrap_socket(s, server_hostname=host)
        s.write(hdr.encode())

        # Read status line
        status_line = s.readline().decode()
        status = int(status_line.split(" ", 2)[1])

        # Read headers
        location = None
        while True:
            hline = s.readline().decode().strip()
            if not hline:
                break
            if hline.lower().startswith("location:"):
                location = hline.split(":", 1)[1].strip()

        if status in (301, 302, 307, 308) and location:
            s.close()
            url = location
            continue

        if status != 200:
            s.close()
            raise RuntimeError("HTTP %d" % status)

        # Stream body to file
        with open(path, "wb") as f:
            while True:
                chunk = s.read(1024)
                if not chunk:
                    break
                f.write(chunk)
        s.close()
        return path

    raise RuntimeError("too many redirects")

print("connecting to WiFi...")
while not wlan.isconnected():
    print("status:", wlan.status())
    print("connected:", wlan.isconnected())
    print("IP configuration:", wlan.ifconfig())

    if (wlan.status() <= 0):
        print("Failed to connect, status:", wlan.status())
        print("Disconnecting due to failed connection attempt...")
        wlan.disconnect()
        time.sleep(1)
        print("Re-attempting connection...")
        wlan.connect(WIFI_SSID, WIFI_PASSWORD)
        time.sleep(1)

    time.sleep(10)

print("Connected")  
print("status:", wlan.status())
print("connected:", wlan.isconnected())
print("IP configuration:", wlan.ifconfig())

print("Downloading firmware from %s..." % FIRMWARE_URL)
try:
    _download_firmware(FIRMWARE_URL)
    print("Download complete.")
except Exception as e:
    print("Download failed:— using existing.")
