"""Clear the user FRAM segment (0x1800-0x19FF) to all 0xFF over SBW."""
import time
from sbw import SBW
from target_power import TargetPower

SBW_PIN_CLOCK = 26
SBW_PIN_DATA = 22
SBW_PIN_POWER = 27

USER_FRAM_START = 0x1800
USER_FRAM_END = 0x19FF

power = TargetPower(SBW_PIN_POWER, 20)
sbw = SBW(SBW_PIN_CLOCK, SBW_PIN_DATA)

power.on()
print("Clearing user FRAM 0x%04X-0x%04X..." % (USER_FRAM_START, USER_FRAM_END))
length = USER_FRAM_END - USER_FRAM_START + 1
ok = sbw.write_bytes(USER_FRAM_START, b'\xff' * length)
if ok:
    print("Cleared %d bytes." % length)
else:
    print("Write failed.")
power.off()
