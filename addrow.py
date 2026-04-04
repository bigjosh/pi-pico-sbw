"""Append rows to a Google Sheet via Apps Script, with offline resilience.

Foreground: writes each request as a file in pending/, never blocks.
Background: drains pending/ in FIFO order, deleting files on success.

The pending/ directory is the queue. Files are written as .tmp_* (invisible
to background), then atomically renamed to req_* (visible). This guarantees
the background never sees a partially written file. Both os.rename() and
os.remove() are atomic on the Pico's littlefs2 filesystem.
"""
import json
import os
import socket
import ssl
import time

PENDING_DIR = "pending"

_seq = 0


def _ensure_dir():
    try:
        os.mkdir(PENDING_DIR)
    except OSError:
        pass


def add_row(url, data):
    """Queue a row for sending. Returns immediately, never blocks.

    The background thread (started via start_background_drain) will
    send it when WiFi is available.
    """
    global _seq
    _seq += 1
    _ensure_dir()
    json_str = json.dumps(data)
    tmp_path = "%s/.tmp_%06d" % (PENDING_DIR, _seq)
    req_path = "%s/req_%06d" % (PENDING_DIR, _seq)
    with open(tmp_path, "w") as f:
        f.write(json_str)
    os.rename(tmp_path, req_path)


def pending_count():
    """Return the number of unsent requests in the queue."""
    try:
        return sum(1 for name in os.listdir(PENDING_DIR) if name.startswith("req_"))
    except OSError:
        return 0


def _get_json(url):
    """GET a URL over HTTPS, return parsed JSON response."""
    proto, _, host_path = url.split("/", 2)
    host_port, path = host_path.split("/", 1)
    path = "/" + path
    port = 443 if proto == "https:" else 80
    if ":" in host_port:
        host, port = host_port.rsplit(":", 1)
        port = int(port)
    else:
        host = host_port

    hdr = "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n" % (path, host)
    s = socket.socket()
    s.connect(socket.getaddrinfo(host, port)[0][-1])
    if proto == "https:":
        s = ssl.wrap_socket(s, server_hostname=host)
    s.write(hdr.encode())

    # Skip status line and headers
    while s.readline().strip():
        pass
    resp_body = s.read()
    s.close()
    return json.loads(resp_body)


def flush_once(url):
    """Try to send one pending request. Returns True if one was sent."""
    try:
        names = sorted(name for name in os.listdir(PENDING_DIR) if name.startswith("req_"))
    except OSError:
        return False

    if not names:
        return False

    name = names[0]
    path = "%s/%s" % (PENDING_DIR, name)
    try:
        with open(path, "r") as f:
            data = json.loads(f.read())
        result = _post_json(url, data)
        if result.get("message") == "Data appended successfully":
            os.remove(path)
            return True
        else:
            print("Sheet rejected row: %s" % result.get("message", "unknown"))
            return False
    except Exception as e:
        print("Send failed: %s" % e)
        return False


def _post_json(url, data, max_redirects=3):
    """POST JSON to a URL, following redirects. Returns parsed response JSON.

    MicroPython's requests library converts POST to GET on redirect,
    which breaks Google Apps Script. This uses raw sockets instead.
    """
    body = json.dumps(data).encode()

    for _ in range(max_redirects + 1):
        proto, _, host_path = url.split("/", 2)
        host_port, path = host_path.split("/", 1)
        path = "/" + path
        use_ssl = proto == "https:"
        port = 443 if use_ssl else 80
        if ":" in host_port:
            host, port = host_port.rsplit(":", 1)
            port = int(port)
        else:
            host = host_port

        hdr = "POST %s HTTP/1.0\r\nHost: %s\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n" % (path, host, len(body))

        s = socket.socket()
        s.connect(socket.getaddrinfo(host, port)[0][-1])
        if use_ssl:
            s = ssl.wrap_socket(s, server_hostname=host)
        s.write(hdr.encode() + body)

        # Read status line
        line = s.readline().decode()
        status = int(line.split(" ", 2)[1])

        # Read headers
        location = None
        while True:
            hline = s.readline().decode().strip()
            if not hline:
                break
            if hline.lower().startswith("location:"):
                location = hline.split(":", 1)[1].strip()

        if status in (301, 302, 307, 308) and location:
            # Google Apps Script redirects POST to a GET response URL
            resp_body = s.read()
            s.close()
            return _get_json(location)

        # Read body
        resp_body = s.read()
        s.close()
        return json.loads(resp_body)

    raise RuntimeError("too many redirects")


