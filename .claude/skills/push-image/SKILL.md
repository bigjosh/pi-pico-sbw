---
name: push-image
description: Copy TSL firmware image from CCS project to the Pico root filesystem
---

Copy the compiled TSL firmware to the Pico using mpremote with PID tracking:

```bash
mpremote connect COM11 cp "D:/Github/TSL-calibre-MSP/CCS Project/Release/tsl-calibre-msp.txt" :tsl-calibre-msp.txt & echo "MPREMOTE_PID:$!"; wait $!
```

Print the result when done.
