# Google Sheets Logging

Log each programming event to a Google Sheet over WiFi. Every time a
target is programmed, a row is appended with the device UUID, timestamp,
firmware hash, MAC address, and pass/fail status.

## How It Works

```
Pico W  →  POST JSON  →  Google Apps Script  →  appends row to Google Sheet
```

Each log request is written to a local file first (write-ahead), then
transmitted over WiFi. If WiFi is unavailable, requests accumulate on
disk and drain automatically when connectivity returns. Nothing is lost.

### Write-ahead queue

The `pending/` directory on the Pico's filesystem is the queue. Each
request is a single JSON file:

1. `add_row()` writes to `pending/.tmp_000042` (invisible to sender)
2. Atomically renames to `pending/req_000042` (visible to sender)
3. `flush_once()` reads the oldest `req_*` file, POSTs it, deletes on success

Both `os.rename()` and `os.remove()` are atomic on the Pico's littlefs2
filesystem, so there's no risk of partial reads even if power is lost
mid-operation.

### Quirks


#### Appscript 302

Google Apps Script returns a 302 redirect on every POST. MicroPython's
`urequests` library silently converts POST to GET on redirect, which
breaks the flow. Our `addrow.py` uses raw sockets to handle this
correctly: POST to the script URL, receive the 302, then GET the
redirect URL to retrieve the JSON response.

#### Micropython thread locks

I originally thought that we could use the the second processor on the Pico to handle all the web logging in the background,
but it turns out that the threads share lots of locks so the background thread ended up slowing down the main
programming thread significantly. For my use case, it takes time fore the person to take the target out of the jig so 
it is OK to have the logging take a couple of blocking seconds, but if you wanted to push that out of the main loop then you could
probably redo the background task in C. 

## Setup

You are welcome to use the sample deploy URL in the `secrets.py` in this repo for testing. Your log entries will appear in the 
public sheet viewable here...
https://docs.google.com/spreadsheets/d/1iP67tK8HS1eW9TOhgdxXeeBYk5jmoF0EW1KPMRkxliM/edit?usp=sharing

...but I might occasionally delete stuff from that sheet so don't use it for production.

Here is how to set up your own private logging sheet...

### 1. Copy the sample spreadsheet

We provide a [sample Google Sheet](https://docs.google.com/spreadsheets/d/1FnmMkalKIRswcSzWMTtvnYA1gJqtGSZJb9MsDqMPEFU)
with the Apps Script already configured.

1. Open the link above
2. **File → Make a copy** to create your own copy
3. Your copy includes the Apps Script — no code to write

### 2. Deploy the Apps Script

The script in your copy needs to be deployed as a web app:

1. Open your copied spreadsheet
2. **Extensions → Apps Script**
3. Click **Deploy → New deployment**
4. Set type to **Web app**
5. Set "Who has access" to **Anyone** (the Pico sends unauthenticated
   requests)
6. Click **Deploy**
7. Copy the deployment URL (starts with
   `https://script.google.com/macros/s/...`)

Note that anyone who knows this URL can append rows to your sheet, so treat it like a secret. 

Each time you edit the script, you must create a **new deployment** for
changes to take effect. The URL changes with each deployment.

### 3. Configure secrets on the Pico

Create `secrets.py` on the Pico (this file is gitignored):

```python
WIFI_SSID = "YourNetwork"
WIFI_PASSWORD = "YourPassword"
SHEETS_URL = "https://script.google.com/macros/s/YOUR_DEPLOYMENT_ID/exec"
```

### 4. Copy the required files to the Pico

In addition to the base programmer files, you need:

| File | Purpose |
|------|---------|
| `addrow.py` | Write-ahead queue and HTTP POST with redirect handling |
| `wifi.py` | WiFi connection management |
| `secrets.py` | Your WiFi and Apps Script credentials |

## Usage

```python
from addrow import add_row, flush_once, pending_count
import wifi
from secrets import WIFI_SSID, WIFI_PASSWORD, SHEETS_URL

wifi.connect(WIFI_SSID, WIFI_PASSWORD)

# Queue a row (instant, writes to disk)
add_row(SHEETS_URL, ["device_uuid", "2026-04-03 12:00:00", "fw_hash", "mac", "pass"])

# Send all pending rows
while pending_count():
    flush_once(SHEETS_URL)
```

`add_row()` never blocks on the network — it just writes a file. Call
`flush_once()` when you're ready to send. Each call sends one row and
returns `True` on success.

See `program_tsl.py` for a complete example integrating WiFi management,
logging, and auto-detect programming in a single loop.

## What the Apps Script does

The script expects a POST body containing a JSON array. It:

1. Parses the JSON array
2. Prepends a server-side UTC timestamp
3. Appends the row to the first sheet in the spreadsheet
4. Returns `{"status": "success", "message": "Data appended successfully"}`

On error it returns `{"status": "error", "message": "..."}` and
`flush_once()` prints the error and retries on the next call.

### Apps Script source

This is the script bound to the sample spreadsheet. Your copy already
has it — this is here for reference only.

```javascript
function doPost(e) {
  try {
    var sheet = SpreadsheetApp.getActiveSpreadsheet().getSheets()[0];
    var values = JSON.parse(e.postData.contents);

    if (!Array.isArray(values) || values.length > 10) {
      return ContentService.createTextOutput(JSON.stringify({
        'status': 'error',
        'message': 'Too many params'
      })).setMimeType(ContentService.MimeType.JSON);
    }

    const now = new Date();
    const timestamp = Utilities.formatDate(now, 'GMT',
        "yyyy-MM-dd'T'HH:mm:ss'Z'");
    values.unshift(timestamp);

    sheet.appendRow(values);

    return ContentService.createTextOutput(JSON.stringify({
      'status': 'success',
      'message': 'Data appended successfully'
    })).setMimeType(ContentService.MimeType.JSON);

  } catch (error) {
    return ContentService.createTextOutput(JSON.stringify({
      'status': 'error',
      'message': error.toString()
    })).setMimeType(ContentService.MimeType.JSON);
  }
}
```

## Limitations

- **WiFi required**: Pico W or Pico 2 W only. Non-W variants can still
  queue rows to disk but can't send them.
- **No authentication**: The Apps Script endpoint is open to anyone with
  the URL. The URL is long and random, providing security through
  obscurity. For production use, consider adding a shared secret in the
  POST body that the script validates.
- **Latency**: Each `flush_once()` takes 1–3 seconds (TLS handshake +
  HTTP round-trip + Google redirect). This happens after programming, not
  during, so it doesn't affect programming speed.
- **Deployment versioning**: Every edit to the Apps Script requires a new
  deployment. The URL changes each time — update `secrets.py` to match.
- **Offline resilience**: Pending rows survive power cycles (they're files
  on disk). They drain automatically when WiFi returns. There is no limit
  on the number of pending rows other than filesystem space.


## Files

| File | Purpose |
|------|---------|
| `addrow.py` | Queue management + HTTP POST with Google redirect handling |
| `wifi.py` | WiFi connect/reconnect helpers |
| `secrets.py` | Credentials (gitignored, create your own) |
| `pending/` | On-device queue directory (created automatically) |
