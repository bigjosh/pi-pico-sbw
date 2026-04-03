import machine

import sbw_native


class SBW:
    def __init__(self, clock_pin, data_pin):
        freq = machine.freq()
        if isinstance(freq, tuple):
            freq = freq[0]
        if freq > sbw_native.SYS_CLK_HZ:
            raise RuntimeError("clk_sys %d Hz exceeds sbw_native limit %d Hz" % (freq, sbw_native.SYS_CLK_HZ))
        self._clk = 1 << clock_pin
        self._dio = 1 << data_pin
        self._clock = machine.Pin(clock_pin, machine.Pin.OUT, value=0)
        self._data = machine.Pin(data_pin, machine.Pin.IN)

    def release(self):
        self._data.init(machine.Pin.IN)
        self._clock.init(machine.Pin.OUT, value=0)

    def read_id(self):
        return sbw_native.read_id(self._clk, self._dio)

    def bypass_test(self):
        return sbw_native.bypass_test(self._clk, self._dio)

    def sync_and_por(self):
        return sbw_native.sync_and_por(self._clk, self._dio)

    def read_mem16(self, address):
        return sbw_native.read_mem16(self._clk, self._dio, address)

    def write_mem16(self, address, value):
        return sbw_native.write_mem16(self._clk, self._dio, address, value)

    def read_block16(self, address, words):
        return sbw_native.read_block16(self._clk, self._dio, address, words)

    def read_bytes(self, address, length):
        if length < 0:
            raise ValueError("length must be non-negative")
        if length == 0:
            return True, b""

        start = address & ~0x1
        end = address + length
        aligned_end = (end + 1) & ~0x1
        word_count = (aligned_end - start) // 2

        ok, payload = self.read_block16(start, word_count)
        if not ok:
            return False, b""

        offset = address - start
        return True, payload[offset : offset + length]

    def write_block16(self, address, data):
        return bool(sbw_native.write_block16(self._clk, self._dio, address, data))

    def write_bytes(self, address, data):
        if not data:
            return True

        start = address & ~0x1
        end = address + len(data)
        aligned_end = (end + 1) & ~0x1
        total_length = aligned_end - start
        offset = address - start

        if start != address or total_length != len(data):
            ok, existing = self.read_block16(start, total_length // 2)
            if not ok:
                return False
            payload = bytearray(existing)
        else:
            payload = bytearray(total_length)

        payload[offset : offset + len(data)] = data
        return self.write_block16(start, bytes(payload))

    def verify_bytes(self, address, expected):
        ok, actual = self.read_bytes(address, len(expected))
        return ok and actual == expected, actual

    def mem_smoke16(self, address, value):
        ok, original = self.read_mem16(address)
        if not ok:
            return False, 0, 0, 0

        ok, test_readback = self.write_mem16(address, value)
        if not ok or test_readback != (value & 0xFFFF):
            return False, original, test_readback, 0

        ok, restored_readback = self.write_mem16(address, original)
        if not ok or restored_readback != original:
            return False, original, test_readback, restored_readback

        return True, original, test_readback, restored_readback
