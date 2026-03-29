"""SBW/JTAG protocol layer using PIO transport.

All JTAG operations are built from packed 4-bit records sent to the PIO
state machine. The PIO drives SBWTCK/SBWTDIO at 10 MHz autonomously
between Python interactions.

Record format (4 bits, LSB first in FIFO word):
  bit 0: TMS
  bit 1: TDI
  bit 2: TMSLDH
  bit 3: DONE
"""

import time

import machine

from sbw_config import (
    BYPASS_EXPECTED,
    BYPASS_PATTERN,
    FULL_EMULATION_MASK,
    GPIO_OE_CLR_ADDR,
    GPIO_OE_SET_ADDR,
    GPIO_OUT_SET_ADDR,
    POWER_MASK,
    SBW_PIN_TARGET_POWER,
    SBW_SYS_CLK_HZ,
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

_JTAG_ID_EXPECTED = const(0x98)
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
# Record packing
# ---------------------------------------------------------------------------

def _rec(tms, tdi, tmsldh=0, done=0):
    """Build a 4-bit record.

    Bit order (shift-right, consumed LSB first by PIO):
      bit 0: TMSLDH  (consumed first)
      bit 1: TMS
      bit 2: TDI
      bit 3: DONE
    """
    return (done << 3) | (tdi << 2) | (tms << 1) | tmsldh


def _records_to_words(records):
    """Pack a list of 4-bit records into 32-bit FIFO words (8 per word)."""
    words = []
    word = 0
    for i, r in enumerate(records):
        pos = (i & 7) << 2  # bit position within the word
        word |= (r & 0xF) << pos
        if (i & 7) == 7:
            words.append(word)
            word = 0
    # Flush partial last word (padding bits after DONE are zero = harmless)
    if len(records) & 7:
        words.append(word)
    return words


# ---------------------------------------------------------------------------
# Sequence builders
# ---------------------------------------------------------------------------

def _go_to_shift_ir(tclk_high):
    """4 records: Run-Test/Idle -> Shift-IR."""
    return [
        _rec(1, 1 if tclk_high else 0),  # Select-DR-Scan
        _rec(1, 1),                       # Select-IR-Scan
        _rec(0, 1),                       # Capture-IR
        _rec(0, 1),                       # Shift-IR
    ]


def _go_to_shift_dr(tclk_high):
    """3 records: Run-Test/Idle -> Shift-DR."""
    return [
        _rec(1, 1 if tclk_high else 0),  # Select-DR-Scan
        _rec(0, 1),                       # Capture-DR
        _rec(0, 1),                       # Shift-DR
    ]


def _finish_shift(tclk_high):
    """2 records: Exit-Shift -> Run-Test/Idle."""
    return [
        _rec(1, 1),                       # Exit1 -> Update (TDI=1 per C reference)
        _rec(0, 1 if tclk_high else 0),   # Update -> RTI
    ]


def _pack_tap_reset():
    """7 records for TAP reset."""
    recs = [_rec(1, 1) for _ in range(6)]
    recs.append(_rec(0, 1, done=1))
    return recs


def _pack_shift_ir8(ir, tclk_high, capture=False):
    """14 records: go_to_shift_ir + 8-bit LSB-first shift + finish_shift.

    Returns (records, data_start, data_count) if capture, else records.
    data_start/data_count identify which TDO bits carry the IR capture.
    """
    recs = _go_to_shift_ir(tclk_high)
    data_start = len(recs)

    shift = ir & 0xFF
    for bit in range(7):
        recs.append(_rec(0, shift & 1))
        shift >>= 1
    recs.append(_rec(1, shift & 1))  # last bit, TMS=1 -> exit

    recs.extend(_finish_shift(tclk_high))
    recs[-1] |= 0x8  # set DONE on last record
    if capture:
        return recs, data_start, 8
    return recs


def _pack_shift_dr16(data, tclk_high, capture=False):
    """21 records: go_to_shift_dr + 16-bit MSB-first shift + finish_shift."""
    recs = _go_to_shift_dr(tclk_high)
    data_start = len(recs)

    shift = data & 0xFFFF
    for bit in range(15):
        recs.append(_rec(0, 1 if (shift & 0x8000) else 0))
        shift <<= 1
    recs.append(_rec(1, 1 if (shift & 0x8000) else 0))

    recs.extend(_finish_shift(tclk_high))
    recs[-1] |= 0x8
    if capture:
        return recs, data_start, 16
    return recs


def _pack_shift_dr20(data, tclk_high):
    """25 records: go_to_shift_dr + 20-bit MSB-first shift + finish_shift."""
    recs = _go_to_shift_dr(tclk_high)

    shift = data & 0x000FFFFF
    for bit in range(19):
        recs.append(_rec(0, 1 if (shift & 0x00080000) else 0))
        shift <<= 1
    recs.append(_rec(1, 1 if (shift & 0x00080000) else 0))

    recs.extend(_finish_shift(tclk_high))
    recs[-1] |= 0x8
    return recs


def _pack_tclk_set(new_level, tclk_high):
    """1 record for TCLK transition."""
    tmsldh = 1 if (tclk_high and not new_level) else 0
    return [_rec(0, 1 if new_level else 0, tmsldh=tmsldh, done=1)]


def _extract_tdo(rx_word, total_records, data_start, data_count):
    """Extract captured TDO bits from an RX word.

    With ISR shift-left: first TDO at bit (total_records - 1), last at bit 0.
    The data bits span positions [total_records - 1 - data_start] down to
    [total_records - data_start - data_count].
    """
    shift = total_records - data_start - data_count
    return (rx_word >> shift) & ((1 << data_count) - 1)


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

    # -- Low-level JTAG helpers --

    def _begin_session(self):
        self._transport.begin_session()
        self._tclk_high = True

    def _end_session(self):
        self._transport.end_session()
        self._tclk_high = True

    def _tap_reset(self):
        recs = _pack_tap_reset()
        self._transport.execute_no_capture(_records_to_words(recs))

    def _tclk_set(self, high):
        recs = _pack_tclk_set(high, self._tclk_high)
        self._transport.execute_no_capture(_records_to_words(recs))
        self._tclk_high = high

    def _shift_ir8(self, ir):
        """Shift 8-bit IR, no capture."""
        recs = _pack_shift_ir8(ir, self._tclk_high, capture=False)
        self._transport.execute_no_capture(_records_to_words(recs))

    def _shift_ir8_capture(self, ir):
        """Shift 8-bit IR, capture TDO."""
        recs, ds, dc = _pack_shift_ir8(ir, self._tclk_high, capture=True)
        total = len(recs)
        rx = self._transport.execute(_records_to_words(recs))
        return _extract_tdo(rx, total, ds, dc)

    def _shift_dr16(self, data):
        """Shift 16-bit DR, no capture."""
        recs = _pack_shift_dr16(data, self._tclk_high, capture=False)
        self._transport.execute_no_capture(_records_to_words(recs))

    def _shift_dr16_capture(self, data):
        """Shift 16-bit DR, capture TDO."""
        recs, ds, dc = _pack_shift_dr16(data, self._tclk_high, capture=True)
        total = len(recs)
        rx = self._transport.execute(_records_to_words(recs))
        return _extract_tdo(rx, total, ds, dc)

    def _shift_dr20(self, data):
        """Shift 20-bit DR, no capture."""
        recs = _pack_shift_dr20(data, self._tclk_high)
        self._transport.execute_no_capture(_records_to_words(recs))

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

        if self._shift_ir8_capture(_IR_CNTRL_SIG_CAPTURE) != _JTAG_ID_EXPECTED:
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
            _SYSCFG0_ADDR, _SYSCFG0_PASSWORD | (low_bits & 0xFF)
        )

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

    # -- DMA block transfer --

    def _dma_transfer(self, fifo_tx, word_count, capture_rx=False):
        """Fire-and-forget DMA: TX feeds PIO, RX drains it.

        Returns the RX buffer (array of raw ISR words) if capture_rx,
        otherwise None.  RX DMA completion signals all words processed.
        """
        import rp2, array
        PIO0_TXF0 = 0x50200010
        PIO0_RXF0 = 0x50200020

        rx_buf = array.array("I", (0 for _ in range(word_count)))

        dma_tx = rp2.DMA()
        dma_rx = rp2.DMA()
        try:
            ctrl_tx = dma_tx.pack_ctrl(treq_sel=0, size=2,
                                       inc_read=True, inc_write=False)
            ctrl_rx = dma_rx.pack_ctrl(treq_sel=4, size=2,
                                       inc_read=False, inc_write=True)

            dma_rx.config(read=PIO0_RXF0, write=rx_buf,
                         count=word_count, ctrl=ctrl_rx, trigger=True)
            dma_tx.config(read=fifo_tx, write=PIO0_TXF0,
                         count=len(fifo_tx), ctrl=ctrl_tx, trigger=True)

            while dma_rx.active():
                pass
        finally:
            dma_tx.close()
            dma_rx.close()

        return rx_buf if capture_rx else None

    # -- Block operations --

    def _begin_read_block_quick(self, address):
        if not self._set_pc(address):
            return False
        self._tclk_set(True)
        self._shift_ir8(_IR_CNTRL_SIG_16BIT)
        self._shift_dr16(0x0501)
        self._shift_ir8(_IR_ADDR_CAPTURE)
        self._shift_ir8(_IR_DATA_QUICK)
        self._tclk_set(True)

        # Pre-compute the per-word FIFO template (same for every word)
        recs = _pack_tclk_set(False, True)  # ClrTCLK (TMSLDH)
        recs[-1] &= ~0x8
        dr_recs, ds, dc = _pack_shift_dr16(0x0000, False, capture=True)
        self._qr_dr_start = len(recs) + ds
        self._qr_dr_count = dc
        recs.extend(dr_recs)
        recs[-1] &= ~0x8
        recs.extend(_pack_tclk_set(True, False))  # SetTCLK
        self._qr_total = len(recs)
        self._qr_words = _records_to_words(recs)
        return True

    def _read_block_quick_word(self):
        """Read one word via quick path, using pre-computed FIFO template."""
        rx = self._transport.execute(self._qr_words)
        self._tclk_high = True
        return _extract_tdo(rx, self._qr_total, self._qr_dr_start, self._qr_dr_count)

    def _begin_write_block_quick(self, address):
        if not self._set_pc((address - 2) & 0x000FFFFF):
            return False
        self._tclk_set(True)
        self._shift_ir8(_IR_CNTRL_SIG_16BIT)
        self._shift_dr16(0x0500)
        self._shift_ir8(_IR_ADDR_CAPTURE)
        self._shift_ir8(_IR_DATA_QUICK)
        self._tclk_set(True)

        self._shift_dr16(0x1111)  # prime pipeline
        self._tclk_set(False)
        self._tclk_set(True)

        # Pre-compute write template with all data TDI bits = 0.
        # We compute masks to scatter 16 data bits into the FIFO words.
        recs = _pack_shift_dr16(0x0000, True, capture=False)
        recs[-1] &= ~0x8
        recs.extend(_pack_tclk_set(False, True))  # ClrTCLK TMSLDH
        recs[-1] &= ~0x8
        recs.extend(_pack_tclk_set(True, False))  # SetTCLK
        base = _records_to_words(recs)
        self._qw_base = (base[0], base[1], base[2])

        # Build scatter masks: for each data bit (MSB first), which
        # FIFO word and bit position it maps to.
        # Data bit N → record index 3+N → TDI at bit ((rec%8)*4)+2
        self._qw_masks = [[], [], []]
        for n in range(16):
            ri = 3 + n
            wi = ri >> 3
            bp = ((ri & 7) << 2) + 2
            self._qw_masks[wi].append((15 - n, bp))  # (src_bit, dst_bit)
        return True

    def _write_block_quick_word(self, data):
        """Write one word via quick path, scattering data bits directly."""
        w0, w1, w2 = self._qw_base
        d = data & 0xFFFF
        for sb, db in self._qw_masks[0]:
            if d & (1 << sb):
                w0 |= (1 << db)
        for sb, db in self._qw_masks[1]:
            if d & (1 << sb):
                w1 |= (1 << db)
        for sb, db in self._qw_masks[2]:
            if d & (1 << sb):
                w2 |= (1 << db)
        self._transport.execute_no_capture((w0, w1, w2))
        self._tclk_high = True

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
            if last_id == _JTAG_ID_EXPECTED:
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

        for _ in range(_JTAG_ATTEMPTS):
            self._begin_session()
            self._tap_reset()
            cpu_ok, _ = self._prepare_cpu()
            if cpu_ok:
                fe_ok, _ = self._in_full_emulation()
                if fe_ok and self._begin_read_block_quick(address):
                    import array
                    # Replicate per-word template using list multiply (fast)
                    fifo_tx = array.array("I", list(self._qr_words) * words)
                    rx_buf = self._dma_transfer(fifo_tx, words, capture_rx=True)
                    buf = bytearray(words * 2)
                    tot = self._qr_total
                    ds = self._qr_dr_start
                    dc = self._qr_dr_count
                    for i in range(words):
                        w = _extract_tdo(rx_buf[i], tot, ds, dc)
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
                                # Pre-compute ALL FIFO words, then tight loop
                                import array
                                fifo = array.array("I", (0 for _ in range(word_count * 3)))
                                for i in range(word_count):
                                    d = data[i * 2] | (data[i * 2 + 1] << 8)
                                    w0, w1, w2 = self._qw_base
                                    for sb, db in self._qw_masks[0]:
                                        if d & (1 << sb): w0 |= 1 << db
                                    for sb, db in self._qw_masks[1]:
                                        if d & (1 << sb): w1 |= 1 << db
                                    for sb, db in self._qw_masks[2]:
                                        if d & (1 << sb): w2 |= 1 << db
                                    idx = i * 3
                                    fifo[idx] = w0
                                    fifo[idx + 1] = w1
                                    fifo[idx + 2] = w2
                                self._dma_transfer(fifo, word_count)
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
        end = address + length
        aligned_end = (end + 1) & ~0x1
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
# Formatting helpers (unchanged from original)
# ---------------------------------------------------------------------------

def format_status(status):
    return "power=%s" % ("on" if status.get("power") else "off")


def format_bypass(ok, captured):
    return "bypass pattern=0x%04X captured=0x%04X expected=0x%04X %s" % (
        BYPASS_PATTERN,
        captured,
        BYPASS_EXPECTED,
        "(expected)" if ok else "(unexpected)",
    )


def format_sync(ok, control_capture):
    state = "(full-emulation)" if ok and (control_capture & FULL_EMULATION_MASK) == FULL_EMULATION_MASK else "(unexpected)"
    return "cntrl-sig=0x%04X %s" % (control_capture, state)
