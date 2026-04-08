"""Cold boot WiFi test. Reads delay from wifi_test_delay.txt, tries one connect."""
import network, time

with open("wifi_test_delay.txt") as f:
    delay = int(f.read().strip())

wlan = network.WLAN(network.STA_IF)
wlan.active(True)
time.sleep_ms(delay)
wlan.connect("josh.com", "nancy123")
t0 = time.ticks_ms()
while time.ticks_diff(time.ticks_ms(), t0) < 10000:
    s = wlan.status()
    if wlan.isconnected():
        print("delay=%dms: OK in %dms" % (delay, time.ticks_diff(time.ticks_ms(), t0)))
        break
    if s < 0 and time.ticks_diff(time.ticks_ms(), t0) > 2000:
        print("delay=%dms: FAIL status=%d at %dms" % (delay, s, time.ticks_diff(time.ticks_ms(), t0)))
        break
    time.sleep_ms(50)
else:
    print("delay=%dms: TIMEOUT status=%d" % (delay, wlan.status()))
