import network
import time

network.country("US")
wlan = network.WLAN(network.STA_IF)
wlan.active(True)

# By default the wireless chip will active power-saving mode when it is idle, which might lead it to being less responsive. If
# you are running a server or need more responsiveness, you can change this by toggling the power mode.
wlan.config(pm = 0xa11140)


# if not wlan.isconnected():
#     print("Connecting to WiFi...")
#     wlan.connect('josh.com', 'nancy123')
#     time.sleep(0.2)
#     print("Waiting to connect:")
#     print(wlan.ifconfig())
#     time.sleep(1)

while True:
    print("status:", wlan.status())
    print("connected:", wlan.isconnected())
    print("IP configuration:", wlan.ifconfig())


    if (wlan.status() <= 0):
        print("Failed to connect, status:", wlan.status())
        print("Disconnecting due to failed connection attempt...")
        wlan.disconnect()
        time.sleep(1)
        print("Re-attempting connection...")
        wlan.connect('josh.com', 'nancy123')
        time.sleep(1)

    time.sleep(10)

