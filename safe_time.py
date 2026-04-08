"""Thread-safe time access for shared RTC between foreground and background."""
import time
import _thread

_lock = _thread.allocate_lock()


def gmtime():
    """Thread-safe wrapper for time.gmtime()."""
    _lock.acquire()
    try:
        return time.gmtime()
    finally:
        _lock.release()


def settime(ntptime):
    """Thread-safe wrapper for ntptime.settime()."""
    _lock.acquire()
    try:
        ntptime.settime()
    finally:
        _lock.release()
