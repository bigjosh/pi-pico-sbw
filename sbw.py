"""SBW/JTAG protocol layer using PIO transport.

All JTAG operations are built from packed 4-bit records sent to the PIO
state machine. The PIO drives SBWTCK/SBWTDIO autonomously between
Python interactions.

Record format (4 bits per SBW bit, shift-right from OSR):
  bit 0: TMSLDH  (consumed first by PIO)
  bit 1: TMS
  bit 2: TDI
  bit 3: DONE
"""

import array
import rp2
import time

import machine
from micropython import const

from sbw_config import (
    BYPASS_EXPECTED,
    BYPASS_PATTERN,
    FULL_EMULATION_MASK,
    GPIO_OE_CLR_ADDR,
    GPIO_OE_SET_ADDR,
    GPIO_OUT_SET_ADDR,
    JTAG_ID_EXPECTED,
    POWER_MASK,
    SBW_PIN_TARGET_POWER,
    SBW_TARGET_POWER_SETTLE_MS,
    ensure_system_clock,
)
from sbw_pio import SBWTransport

# JTAG IR instructions
_IR_CNTRL_SIG_16BIT = const(0x13)
_IR_CNTRL_SIG_CAPTURE = const(0x14)
_IR_DATA_16BIT = const(0x41)
_IR_DATA_CAPTURE = const(0x42)
_IR_DATA_QUICK = const(0x43)
_IR_ADDR_16BIT = const(0x83)
_IR_ADDR_CAPTURE = const(0x84)
_IR_DATA_TO_ADDR = const(0x85)
_IR_BYPASS = const(0xFF)

_JTAG_SYNC_RETRIES = const(50)
_JTAG_SYNC_MASK = const(0x0200)
_JTAG_ATTEMPTS = const(3)
_SAFE_ACCESS_PC = const(0x0004)
_NOP = const(0x4303)

_SYSCFG0_ADDR = const(0x0160)
_SYSCFG0_PASSWORD = const(0xA500)
_SYSCFG0_PFWP = const(0x0001)
_SYSCFG0_DFWP = const(0x0002)
_WDTCTL_ADDR = const(0x01CC)
_WDTCTL_HOLD = const(0x5A80)

_INFO_FRAM_START = const(0x1800)
_INFO_FRAM_END = const(0x19FF)
_MAIN_FRAM_START = const(0xC400)
_MAIN_FRAM_END = const(0xFFFF)


# ---------------------------------------------------------------------------
# Record packing (internal)
# ---------------------------------------------------------------------------

def _rec(tms, tdi, tmsldh=0, done=0):
    return (done << 3) | (tdi << 2) | (tms << 1) | tmsldh


def _records_to_words(records):
    words = []
    word = 0
    for i, r in enumerate(records):
        word |= (r & 0xF) << ((i & 7) << 2)
        if (i & 7) == 7:
            words.append(word)
            word = 0
    if len(records) & 7:
        words.append(word)
    return words


def _go_to_shift_ir(tclk_high):
    return [_rec(1, 1 if tclk_high else 0), _rec(1, 1), _rec(0, 1), _rec(0, 1)]


def _go_to_shift_dr(tclk_high):
    return [_rec(1, 1 if tclk_high else 0), _rec(0, 1), _rec(0, 1)]


def _finish_shift(tclk_high):
    return [_rec(1, 1), _rec(0, 1 if tclk_high else 0)]


# ---------------------------------------------------------------------------
# Stream builders — each returns (array.array('I'), n_done)
# ---------------------------------------------------------------------------

def build_tap_reset():
    """7-record TAP reset. Returns (tx, 1)."""
    recs = [_rec(1, 1) for _ in range(6)]
    recs.append(_rec(0, 1, done=1))
    return array.array("I", _records_to_words(recs)), 1


def build_shift_ir8(ir, tclk_high):
    """14-record IR shift. Returns (tx, 1)."""
    recs = _go_to_shift_ir(tclk_high)
    shift = ir & 0xFF
    for _ in range(7):
        recs.append(_rec(0, shift & 1))
        shift >>= 1
    recs.append(_rec(1, shift & 1))
    recs.extend(_finish_shift(tclk_high))
    recs[-1] |= 0x8
    return array.array("I", _records_to_words(recs)), 1


def build_shift_dr16(data, tclk_high):
    """21-record DR16 shift. Returns (tx, 1).
    TDO extraction: _extract_dr16(rx_word)."""
    recs = _go_to_shift_dr(tclk_high)
    shift = data & 0xFFFF
    for _ in range(15):
        recs.append(_rec(0, 1 if (shift & 0x8000) else 0))
        shift <<= 1
    recs.append(_rec(1, 1 if (shift & 0x8000) else 0))
    recs.extend(_finish_shift(tclk_high))
    recs[-1] |= 0x8
    return array.array("I", _records_to_words(recs)), 1


def build_shift_dr20(data, tclk_high):
    """25-record DR20 shift. Returns (tx, 1)."""
    recs = _go_to_shift_dr(tclk_high)
    shift = data & 0x000FFFFF
    for _ in range(19):
        recs.append(_rec(0, 1 if (shift & 0x00080000) else 0))
        shift <<= 1
    recs.append(_rec(1, 1 if (shift & 0x00080000) else 0))
    recs.extend(_finish_shift(tclk_high))
    recs[-1] |= 0x8
    return array.array("I", _records_to_words(recs)), 1


def build_tclk_set(new_level, tclk_high):
    """1-record TCLK transition. Returns (tx, 1)."""
    tmsldh = 1 if (tclk_high and not new_level) else 0
    recs = [_rec(0, 1 if new_level else 0, tmsldh=tmsldh, done=1)]
    return array.array("I", _records_to_words(recs)), 1


# TDO extraction constants for IR8 and DR16 captures
_IR8_TOTAL = const(14)
_IR8_DATA_START = const(4)
_IR8_DATA_COUNT = const(8)
_IR8_SHIFT = const(2)  # _IR8_TOTAL - _IR8_DATA_START - _IR8_DATA_COUNT

_DR16_TOTAL = const(21)
_DR16_DATA_START = const(3)
_DR16_DATA_COUNT = const(16)
_DR16_SHIFT = const(2)  # _DR16_TOTAL - _DR16_DATA_START - _DR16_DATA_COUNT


def extract_ir8(rx_word):
    """Extract 8-bit IR capture from RX word."""
    return (rx_word >> _IR8_SHIFT) & 0xFF


def extract_dr16(rx_word):
    """Extract 16-bit DR capture from RX word."""
    return (rx_word >> _DR16_SHIFT) & 0xFFFF


def build_read_quick_word():
    """Per-word quick read: ClrTCLK + DR16(0) + SetTCLK.
    Returns (tx, 1). Use extract_quick_read() on the RX word.

    23 records total: 1(ClrTCLK) + 3(go_dr) + 16(data) + 2(finish) + 1(SetTCLK).
    DR16 data starts at record 4, shift = 23 - 4 - 16 = 3.
    """
    recs = [_rec(0, 0, tmsldh=1)]  # ClrTCLK (TMSLDH)
    recs.extend(_go_to_shift_dr(False))
    for _ in range(15):
        recs.append(_rec(0, 0))
    recs.append(_rec(1, 0))
    recs.extend(_finish_shift(False))
    recs.append(_rec(0, 1, done=1))  # SetTCLK
    return array.array("I", _records_to_words(recs)), 1


_QUICK_READ_SHIFT = const(3)  # 23 total - 4 start - 16 data


def extract_quick_read(rx_word):
    """Extract 16-bit data from quick-read RX word (23-record template)."""
    return (rx_word >> _QUICK_READ_SHIFT) & 0xFFFF


def build_write_quick_word(data, qw_base, qw_masks):
    """Per-word quick write: DR16(data) + ClrTCLK + SetTCLK.
    Returns (tx, 1)."""
    w0, w1, w2 = qw_base
    d = data & 0xFFFF
    for sb, db in qw_masks[0]:
        if d & (1 << sb):
            w0 |= 1 << db
    for sb, db in qw_masks[1]:
        if d & (1 << sb):
            w1 |= 1 << db
    for sb, db in qw_masks[2]:
        if d & (1 << sb):
            w2 |= 1 << db
    return array.array("I", (w0, w1, w2)), 1


def build_write_block_stream(data_bytes, qw_base, qw_masks):
    """Pre-compute entire block write TX stream.
    Returns (tx_array, word_count)."""
    word_count = len(data_bytes) // 2
    tx = array.array("I", (0 for _ in range(word_count * 3)))
    for i in range(word_count):
        d = data_bytes[i * 2] | (data_bytes[i * 2 + 1] << 8)
        w0, w1, w2 = qw_base
        for sb, db in qw_masks[0]:
            if d & (1 << sb):
                w0 |= 1 << db
        for sb, db in qw_masks[1]:
            if d & (1 << sb):
                w1 |= 1 << db
        for sb, db in qw_masks[2]:
            if d & (1 << sb):
                w2 |= 1 << db
        idx = i * 3
        tx[idx] = w0
        tx[idx + 1] = w1
        tx[idx + 2] = w2
    return tx, word_count


def build_read_block_stream(words, per_word_tx):
    """Pre-compute entire block read TX stream by replicating template.
    Returns (tx_array, word_count)."""
    tx = array.array("I", list(per_word_tx) * words)
    return tx, words


# ---------------------------------------------------------------------------
# SBW Native interface (PIO-based)
# ---------------------------------------------------------------------------

class SBWNative:
    def __init__(self, power_mask=POWER_MASK):
        ensure_system_clock()
        self._transport = SBWTransport()
        self._power_pin = machine.Pin(SBW_PIN_TARGET_POWER, machine.Pin.IN)
        self._power_mask = power_mask
        self._power_enabled = False
        self._tclk_high = True

    def pins(self):
        from sbw_pio import SBW_PIN_CLOCK, SBW_PIN_DATA
        return (
            ("SBWTCK", SBW_PIN_CLOCK),
            ("SBWTDIO", SBW_PIN_DATA),
            ("VCC", SBW_PIN_TARGET_POWER),
        )

    def status(self):
        return {"power": self._power_enabled}

    def release(self):
        self._transport.release()
        self._tclk_high = True

    def power_on(self):
        self.release()
        machine.mem32[GPIO_OUT_SET_ADDR] = self._power_mask
        machine.mem32[GPIO_OE_SET_ADDR] = self._power_mask
        self._power_enabled = True
        time.sleep_ms(SBW_TARGET_POWER_SETTLE_MS)

    def power_off(self):
        self.release()
        machine.mem32[GPIO_OE_CLR_ADDR] = self._power_mask
        self._power_pin.init(machine.Pin.IN)
        self._power_enabled = False

    # -- Low-level helpers: execute one stream via sm.put/get --
    # (DMA per-call is too expensive; sm.put/get is fast for small ops)

    def _exec(self, tx):
        """Execute a single-DONE stream, discard RX."""
        self._transport.execute_no_capture(tx)

    def _exec_capture(self, tx):
        """Execute a single-DONE stream, return RX word."""
        return self._transport.execute(tx)

    def _begin_session(self):
        self._transport.begin_session()
        self._tclk_high = True

    def _end_session(self):
        self._transport.end_session()
        self._tclk_high = True

    def _tap_reset(self):
        tx, _ = build_tap_reset()
        self._exec(tx)

    def _tclk_set(self, high):
        tx, _ = build_tclk_set(high, self._tclk_high)
        self._exec(tx)
        self._tclk_high = high

    def _shift_ir8(self, ir):
        tx, _ = build_shift_ir8(ir, self._tclk_high)
        self._exec(tx)

    def _shift_ir8_capture(self, ir):
        tx, _ = build_shift_ir8(ir, self._tclk_high)
        return extract_ir8(self._exec_capture(tx))

    def _shift_dr16(self, data):
        tx, _ = build_shift_dr16(data, self._tclk_high)
        self._exec(tx)

    def _shift_dr16_capture(self, data):
        tx, _ = build_shift_dr16(data, self._tclk_high)
        return extract_dr16(self._exec_capture(tx))

    def _shift_dr20(self, data):
        tx, _ = build_shift_dr20(data, self._tclk_high)
        self._exec(tx)

    # -- JTAG protocol operations --

    def _read_control_signal(self):
        self._shift_ir8(_IR_CNTRL_SIG_CAPTURE)
        return self._shift_dr16_capture(0x0000)

    def _in_full_emulation(self):
        status = self._read_control_signal()
        return (status & FULL_EMULATION_MASK) == FULL_EMULATION_MASK, status

    def _sync_cpu(self):
        self._shift_ir8(_IR_CNTRL_SIG_16BIT)
        self._shift_dr16(0x1501)
        if self._shift_ir8_capture(_IR_CNTRL_SIG_CAPTURE) != JTAG_ID_EXPECTED:
            return False
        for _ in range(_JTAG_SYNC_RETRIES):
            if (self._shift_dr16_capture(0x0000) & _JTAG_SYNC_MASK) != 0:
                return True
        return False

    def _execute_por(self):
        self._tclk_set(False)
        self._tclk_set(True)
        self._shift_ir8(_IR_CNTRL_SIG_16BIT)
        self._shift_dr16(0x0C01)
        self._shift_dr16(0x0401)
        self._shift_ir8(_IR_DATA_16BIT)
        self._tclk_set(False)
        self._tclk_set(True)
        self._tclk_set(False)
        self._tclk_set(True)
        self._shift_dr16(_SAFE_ACCESS_PC)
        self._tclk_set(False)
        self._tclk_set(True)
        self._shift_ir8(_IR_DATA_CAPTURE)
        self._tclk_set(False)
        self._tclk_set(True)
        self._tclk_set(False)
        self._tclk_set(True)
        self._shift_ir8(_IR_CNTRL_SIG_16BIT)
        self._shift_dr16(0x0501)
        self._tclk_set(False)
        self._tclk_set(True)
        self._shift_ir8(_IR_CNTRL_SIG_CAPTURE)
        status = self._shift_dr16_capture(0x0000)
        return (status & FULL_EMULATION_MASK) == FULL_EMULATION_MASK, status

    def _disable_watchdog(self):
        return self._write_mem16_internal(_WDTCTL_ADDR, _WDTCTL_HOLD)

    def _prepare_cpu(self):
        if not self._sync_cpu():
            return False, 0
        ok, status = self._execute_por()
        if not ok:
            return False, status
        if not self._disable_watchdog():
            return False, status
        return True, status

    # -- Memory operations --

    def _write_mem16_sequence(self, address, data):
        self._tclk_set(False)
        self._shift_ir8(_IR_CNTRL_SIG_16BIT)
        self._shift_dr16(0x0500)
        self._shift_ir8(_IR_ADDR_16BIT)
        self._shift_dr20(address & 0x000FFFFF)
        self._tclk_set(True)
        self._shift_ir8(_IR_DATA_TO_ADDR)
        self._shift_dr16(data)
        self._tclk_set(False)
        self._shift_ir8(_IR_CNTRL_SIG_16BIT)
        self._shift_dr16(0x0501)
        self._tclk_set(True)
        self._tclk_set(False)
        self._tclk_set(True)

    def _read_mem16_internal(self, address):
        ok, _ = self._in_full_emulation()
        if not ok:
            return False, 0
        self._tclk_set(False)
        self._shift_ir8(_IR_CNTRL_SIG_16BIT)
        self._shift_dr16(0x0501)
        self._shift_ir8(_IR_ADDR_16BIT)
        self._shift_dr20(address & 0x000FFFFF)
        self._shift_ir8(_IR_DATA_TO_ADDR)
        self._tclk_set(True)
        self._tclk_set(False)
        value = self._shift_dr16_capture(0x0000)
        self._tclk_set(True)
        self._tclk_set(False)
        self._tclk_set(True)
        ok, _ = self._in_full_emulation()
        return ok, value

    def _write_mem16_internal(self, address, value):
        ok, _ = self._in_full_emulation()
        if not ok:
            return False
        self._write_mem16_sequence(address, value)
        ok, _ = self._in_full_emulation()
        return ok

    def _set_pc(self, address):
        pc = address & 0x000FFFFF
        mova_imm20_pc = (((pc >> 16) & 0xF) << 8) | 0x0080
        ok, _ = self._in_full_emulation()
        if not ok:
            return False
        self._tclk_set(False)
        self._shift_ir8(_IR_DATA_16BIT)
        self._tclk_set(True)
        self._shift_dr16(mova_imm20_pc)
        self._tclk_set(False)
        self._shift_ir8(_IR_CNTRL_SIG_16BIT)
        self._shift_dr16(0x1400)
        self._shift_ir8(_IR_DATA_16BIT)
        self._tclk_set(False)
        self._tclk_set(True)
        self._shift_dr16(pc & 0xFFFF)
        self._tclk_set(False)
        self._tclk_set(True)
        self._shift_dr16(_NOP)
        self._tclk_set(False)
        self._shift_ir8(_IR_ADDR_CAPTURE)
        self._shift_dr20(0x00000)
        return True

    # -- FRAM protection --

    def _fram_protection_mask(self, address):
        masked = address & 0x000FFFFF
        if _INFO_FRAM_START <= masked <= _INFO_FRAM_END:
            return _SYSCFG0_DFWP
        if _MAIN_FRAM_START <= masked <= _MAIN_FRAM_END:
            return _SYSCFG0_PFWP
        return 0

    def _block_protection_mask(self, address, word_count):
        start = address & 0x000FFFFF
        if start & 1:
            return None
        if word_count == 0:
            return self._fram_protection_mask(start)
        max_words = (0x00100000 - start) >> 1
        if word_count > max_words:
            return None
        first = self._fram_protection_mask(start)
        last = self._fram_protection_mask(start + ((word_count - 1) << 1))
        if first != last:
            return None
        return first

    def _write_syscfg0_low(self, low_bits):
        return self._write_mem16_internal(
            _SYSCFG0_ADDR, _SYSCFG0_PASSWORD | (low_bits & 0xFF))

    def _unlock_fram(self, address):
        prot = self._fram_protection_mask(address)
        if prot == 0:
            return True, 0, False
        ok, syscfg0 = self._read_mem16_internal(_SYSCFG0_ADDR)
        if not ok:
            return False, 0, False
        saved = syscfg0 & 0xFF
        if (saved & prot) == 0:
            return True, saved, False
        if not self._write_syscfg0_low(saved & ~prot):
            return False, saved, False
        return True, saved, True

    def _restore_fram(self, saved, changed):
        if not changed:
            return True
        return self._write_syscfg0_low(saved)

    # -- Block quick-path helpers --

    def _compute_write_templates(self):
        """Compute base FIFO words and scatter masks for quick writes."""
        recs = _go_to_shift_dr(True)
        for _ in range(15):
            recs.append(_rec(0, 0))
        recs.append(_rec(1, 0))
        recs.extend(_finish_shift(True))
        # ClrTCLK (TMSLDH) + SetTCLK (with DONE)
        recs.append(_rec(0, 0, tmsldh=1))
        recs.append(_rec(0, 1, done=1))
        base = _records_to_words(recs)
        qw_base = (base[0], base[1], base[2])
        qw_masks = [[], [], []]
        for n in range(16):
            ri = 3 + n
            wi = ri >> 3
            bp = ((ri & 7) << 2) + 2
            qw_masks[wi].append((15 - n, bp))
        return qw_base, qw_masks

    def _begin_read_block_quick(self, address):
        if not self._set_pc(address):
            return False
        self._tclk_set(True)
        self._shift_ir8(_IR_CNTRL_SIG_16BIT)
        self._shift_dr16(0x0501)
        self._shift_ir8(_IR_ADDR_CAPTURE)
        self._shift_ir8(_IR_DATA_QUICK)
        self._tclk_set(True)
        return True

    def _begin_write_block_quick(self, address):
        if not self._set_pc((address - 2) & 0x000FFFFF):
            return False
        self._tclk_set(True)
        self._shift_ir8(_IR_CNTRL_SIG_16BIT)
        self._shift_dr16(0x0500)
        self._shift_ir8(_IR_ADDR_CAPTURE)
        self._shift_ir8(_IR_DATA_QUICK)
        self._tclk_set(True)
        self._shift_dr16(0x1111)
        self._tclk_set(False)
        self._tclk_set(True)
        return True

    def _finish_write_block_quick(self):
        self._shift_ir8(_IR_CNTRL_SIG_16BIT)
        self._shift_dr16(0x0501)
        self._tclk_set(True)
        self._tclk_set(False)
        self._tclk_set(True)
        ok, _ = self._in_full_emulation()
        return ok

    # -- Public API (each operation manages its own session) --

    def read_id(self):
        last_id = 0
        ok = False
        for _ in range(_JTAG_ATTEMPTS):
            self._begin_session()
            self._tap_reset()
            last_id = self._shift_ir8_capture(_IR_CNTRL_SIG_CAPTURE)
            self._end_session()
            if last_id == JTAG_ID_EXPECTED:
                ok = True
                break
        return ok, last_id

    def bypass_test(self):
        captured = 0
        ok = False
        for _ in range(_JTAG_ATTEMPTS):
            self._begin_session()
            self._tap_reset()
            self._shift_ir8(_IR_BYPASS)
            captured = self._shift_dr16_capture(BYPASS_PATTERN)
            self._end_session()
            if captured == BYPASS_EXPECTED:
                ok = True
                break
        return ok, captured

    def sync_and_por(self):
        for _ in range(_JTAG_ATTEMPTS):
            self._begin_session()
            self._tap_reset()
            ok, status = self._prepare_cpu()
            self._end_session()
            if ok:
                return ok, status
        return False, 0

    def read_mem16(self, address):
        data = 0
        ok = False
        for _ in range(_JTAG_ATTEMPTS):
            self._begin_session()
            self._tap_reset()
            cpu_ok, _ = self._prepare_cpu()
            if cpu_ok:
                ok, data = self._read_mem16_internal(address)
            self._end_session()
            if ok:
                break
        return ok, data

    def write_mem16(self, address, value):
        readback = 0
        ok = False
        for _ in range(_JTAG_ATTEMPTS):
            self._begin_session()
            self._tap_reset()
            cpu_ok, _ = self._prepare_cpu()
            if cpu_ok:
                ok_unlock, saved, changed = self._unlock_fram(address)
                if ok_unlock:
                    ok_write = self._write_mem16_internal(address, value)
                    if ok_write:
                        ok_read, readback = self._read_mem16_internal(address)
                        ok = ok_read and readback == value
                    self._restore_fram(saved, changed)
            self._end_session()
            if ok:
                break
        return ok, readback

    def read_block16(self, address, words):
        if words == 0:
            return True, b""

        result = b""
        ok = False
        # Pre-build the per-word read template
        per_word_tx, _ = build_read_quick_word()

        for _ in range(_JTAG_ATTEMPTS):
            self._begin_session()
            self._tap_reset()
            cpu_ok, _ = self._prepare_cpu()
            if cpu_ok:
                fe_ok, _ = self._in_full_emulation()
                if fe_ok and self._begin_read_block_quick(address):
                    tx, n = build_read_block_stream(words, per_word_tx)
                    rx = array.array("I", (0 for _ in range(n)))
                    self._transport.execute_pio(tx, rx)
                    buf = bytearray(words * 2)
                    for i in range(words):
                        w = extract_quick_read(rx[i])
                        buf[i * 2] = w & 0xFF
                        buf[i * 2 + 1] = (w >> 8) & 0xFF
                    result = bytes(buf)
                    self._tclk_high = True
                    ok = True
            self._end_session()
            if ok:
                break
        return ok, result

    def write_block16(self, address, data):
        if len(data) == 0:
            return True

        word_count = len(data) // 2
        block_mask = self._block_protection_mask(address, word_count)
        if block_mask is None:
            return False

        ok = False
        for _ in range(_JTAG_ATTEMPTS):
            self._begin_session()
            self._tap_reset()
            cpu_ok, _ = self._prepare_cpu()
            if cpu_ok:
                fe_ok, _ = self._in_full_emulation()
                if fe_ok:
                    ok_unlock, saved, changed = self._unlock_fram(address)
                    if ok_unlock:
                        if block_mask == 0:
                            for i in range(word_count):
                                w = data[i * 2] | (data[i * 2 + 1] << 8)
                                self._write_mem16_sequence(address + i * 2, w)
                            write_ok, _ = self._in_full_emulation()
                        else:
                            if self._begin_write_block_quick(address):
                                qw_base, qw_masks = self._compute_write_templates()
                                tx, n = build_write_block_stream(data, qw_base, qw_masks)
                                rx = array.array("I", (0 for _ in range(n)))
                                self._transport.execute_pio(tx, rx)
                                self._tclk_high = True
                                write_ok = self._finish_write_block_quick()
                            else:
                                write_ok = False
                        restore_ok = self._restore_fram(saved, changed)
                        ok = write_ok and restore_ok
            self._end_session()
            if ok:
                break
        return ok

    # -- Byte-level helpers --

    def read_bytes(self, address, length):
        if length <= 0:
            return True, b""
        start = address & ~0x1
        aligned_end = ((address + length) + 1) & ~0x1
        word_count = (aligned_end - start) // 2
        ok, payload = self.read_block16(start, word_count)
        if not ok:
            return False, b""
        offset = address - start
        return True, payload[offset: offset + length]

    def write_bytes(self, address, data):
        if not data:
            return True
        start = address & ~0x1
        aligned_end = ((address + len(data)) + 1) & ~0x1
        total_length = aligned_end - start
        offset = address - start
        if start != address or total_length != len(data):
            ok, existing = self.read_block16(start, total_length // 2)
            if not ok:
                return False
            payload = bytearray(existing)
        else:
            payload = bytearray(total_length)
        payload[offset: offset + len(data)] = data
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


# ---------------------------------------------------------------------------
# Formatting helpers
# ---------------------------------------------------------------------------

def format_status(status):
    return "power=%s" % ("on" if status.get("power") else "off")


def format_bypass(ok, captured):
    return "bypass pattern=0x%04X captured=0x%04X expected=0x%04X %s" % (
        BYPASS_PATTERN, captured, BYPASS_EXPECTED,
        "(expected)" if ok else "(unexpected)")


def format_sync(ok, control_capture):
    state = "(full-emulation)" if ok and (control_capture & FULL_EMULATION_MASK) == FULL_EMULATION_MASK else "(unexpected)"
    return "cntrl-sig=0x%04X %s" % (control_capture, state)
