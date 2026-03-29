"""PIO-based SBW transport for MSP430 JTAG over Spy-Bi-Wire.

FIFO record format (4 bits, shift-right):
  bit 0: TMSLDH  (affects BOTH slot 1 and slot 2)
  bit 1: TMS
  bit 2: TDI
  bit 3: DONE

When TMSLDH=0: normal SBW bit
  Slot 1: TMS + clock fall simultaneous
  Slot 2: TDI + clock fall simultaneous

When TMSLDH=1: TCLK mode (used for ClrTCLK)
  Slot 1: TMS + clock fall, then bring data HIGH during LOW (TMSLDH)
  Slot 2: keep current data, clock fall, wait 50ns, drive TDI (new),
          wait 50ns, clock rise  (centered transition)
"""

import rp2
import machine
import time
from micropython import const

SBW_PIN_CLOCK = const(2)
SBW_PIN_DATA = const(3)

_PIO_FREQ = const(100_000_000)


@rp2.asm_pio(
    out_init=rp2.PIO.OUT_HIGH,
    set_init=rp2.PIO.OUT_HIGH,
    sideset_init=rp2.PIO.OUT_HIGH,
    out_shiftdir=rp2.PIO.SHIFT_RIGHT,
    in_shiftdir=rp2.PIO.SHIFT_LEFT,
    autopull=True,
    pull_thresh=32,
    autopush=False,
)
def sbw_packet_program():
    # 22 instructions.

    wrap_target()
    label("top")

    # -- Read TMSLDH flag --
    out(x, 1)                          .side(1)

    # -- Slot 1: TMS  (data + clock-fall simultaneous) --
    out(pins, 1)                       .side(0)       # TMS + falls
    jmp(not_x, "tms_norm")             .side(0)

    # TMSLDH: bring data HIGH centered in LOW
    nop()                              .side(0)  [1]  # 20ns
    set(pins, 1)                       .side(0)  [1]  # HIGH, 20ns
    jmp("slot2")                       .side(1)       # rises

    label("tms_norm")
    nop()                              .side(0)  [3]  # 40ns LOW

    # -- Slot 2: TDI --
    label("slot2")
    nop()                              .side(1)       # rises (common)
    jmp(not_x, "tdi_norm")             .side(1)       # branch on TMSLDH

    # TCLK mode: centered transition in LOW phase
    nop()                              .side(0)  [4]  # keep current data, 50ns LOW
    out(pins, 1)                       .side(0)  [4]  # drive NEW tclk, 50ns LOW
    jmp("tdo")                         .side(1)       # rises

    # Normal: TDI + clock-fall simultaneous
    label("tdi_norm")
    out(pins, 1)                       .side(0)  [4]  # TDI + falls, 50ns
    nop()                              .side(1)       # rises

    # -- Slot 3: TDO  (sample before rise) --
    label("tdo")
    set(pindirs, 0)                    .side(1)       # release on rise
    nop()                              .side(0) [14]  # falls, 150ns settle
    in_(pins, 1)                       .side(1)       # SAMPLE before rise
    set(pindirs, 1)                    .side(1)       # re-acquire

    # -- DONE --
    out(y, 1)                          .side(1)
    jmp(not_y, "top")                  .side(1)

    push(block)                        .side(1)
    out(null, 32)                      .side(1)
    wrap()


class SBWTransport:
    def __init__(self, pio_id=0, sm_id=0):
        self._clock_pin = machine.Pin(SBW_PIN_CLOCK)
        self._data_pin = machine.Pin(SBW_PIN_DATA)
        self._sm = None
        self._pio_id = pio_id
        self._sm_id = sm_id

    def deinit(self):
        if self._sm is not None:
            self._sm.active(0); self._sm = None

    def release(self):
        if self._sm is not None:
            self._sm.active(0)
        self._clock_pin.init(machine.Pin.OUT, value=0)
        self._data_pin.init(machine.Pin.IN)

    def entry_rst_high(self):
        clk, dat = self._clock_pin, self._data_pin
        dat.init(machine.Pin.IN); clk.value(0); time.sleep_ms(4)
        dat.init(machine.Pin.OUT, value=1); clk.value(1); time.sleep_ms(20)
        time.sleep_us(60); clk.value(0); time.sleep_us(1); clk.value(1); time.sleep_us(60)
        time.sleep_ms(5)

    def entry_rst_low(self):
        clk, dat = self._clock_pin, self._data_pin
        dat.init(machine.Pin.IN); clk.value(0); time.sleep_ms(1)
        dat.init(machine.Pin.OUT, value=0); time.sleep_ms(50)
        clk.value(1); time.sleep_ms(100)
        dat.value(1); time.sleep_us(40); clk.value(0); time.sleep_us(1); clk.value(1); time.sleep_us(40)
        time.sleep_ms(5)

    def _activate_pio(self):
        import gc
        if self._sm is not None:
            self._sm.active(0); self._sm = None; gc.collect()
        self._sm = rp2.StateMachine(
            self._sm_id, sbw_packet_program, freq=_PIO_FREQ,
            sideset_base=self._clock_pin, out_base=self._data_pin,
            set_base=self._data_pin, in_base=self._data_pin)
        self._sm.put(0x0000000E)  # prime: TMS=1, TDI=1, DONE=1
        self._sm.exec("pull(block) .side(1)")
        self._sm.active(1)
        self._sm.get()

    def begin_session(self):
        self.release(); time.sleep_ms(15)
        self.entry_rst_high(); self._activate_pio()

    def end_session(self):
        self.release(); time.sleep_us(200)

    def execute(self, fifo_words):
        sm = self._sm
        for w in fifo_words: sm.put(w)
        return sm.get()

    def execute_no_capture(self, fifo_words):
        sm = self._sm
        for w in fifo_words: sm.put(w)
        sm.get()
