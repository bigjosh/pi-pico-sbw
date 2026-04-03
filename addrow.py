"""Append a row to a Google Sheet via Apps Script, with offline resilience.

Each request is first written to the pending file (write-ahead), then all
pending requests are drained in FIFO order. If sending fails, remaining
entries stay in the pending file and will be retried on the next call.

Requires wifi.py for connection management and secrets.py for credentials.
"""
import json
import os

import urequests as requests

PENDING_FILE = "log_pending.json"


def add_row(url, data):
    """Append data to the pending file, then try to flush all pending rows.

    Assumes WiFi is already connected. Returns True if all pending rows
    (including this one) were sent successfully.
    """
    _append_pending(data)
    return _flush_pending(url)


def _append_pending(data):
    with open(PENDING_FILE, "a") as f:
        f.write(json.dumps(data) + "\n")


def _flush_pending(url):
    try:
        with open(PENDING_FILE, "r") as f:
            lines = f.readlines()
    except OSError:
        return True

    sent = 0
    for line in lines:
        line = line.strip()
        if not line:
            sent += 1
            continue
        if not _try_send(url, line):
            break
        sent += 1

    remaining = lines[sent:]
    if remaining:
        with open(PENDING_FILE, "w") as f:
            f.writelines(remaining)
        return False
    else:
        try:
            os.remove(PENDING_FILE)
        except OSError:
            pass
        return True


def _try_send(url, json_str):
    try:
        response = requests.post(url, data=json_str, headers={"Content-Type": "application/json"})
        result = response.json()
        response.close()
        ok = result.get("message") == "Data appended successfully"
        if ok:
            print("Row sent.")
        else:
            print("Sheet rejected row: %s" % result.get("message", "unknown"))
        return ok
    except Exception as e:
        print("Send failed: %s" % e)
        return False
