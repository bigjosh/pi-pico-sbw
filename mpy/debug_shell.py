from sbw import SBWNative, format_bypass, format_status, format_sync
from sbw_config import bytes_to_words_le
from testsuite import bench_block_roundtrip


def _parse_u32(token):
    return int(token, 0)


def _print_help():
    print("Commands:")
    print("  help")
    print("  pins")
    print("  status")
    print("  power-on")
    print("  power-off")
    print("  read-jtagid")
    print("  bypass-test")
    print("  sync-por")
    print("  read-mem16 <addr>")
    print("  read-block16 <addr> <words>")
    print("  write-read-mem16 <addr> <value>")
    print("  fram-smoke16 <addr> <value>")
    print("  fram-bench <addr> <words>")


def _print_pins(sbw):
    for label, pin in sbw.pins():
        print("%-7s GP%d" % (label, pin))
    print("GND     Pico GND")


def _power_pin(sbw):
    return sbw.pins()[2][1]


def _require_power(sbw):
    if sbw.status()["power"]:
        return True
    print("target power is off")
    return False


def repl():
    sbw = SBWNative()
    print("")
    print("pi-pico-sbw micropython shell")
    _print_pins(sbw)
    _print_help()

    while True:
        try:
            line = input("> ").strip()
        except (EOFError, KeyboardInterrupt):
            print("")
            break

        if not line:
            continue

        argv = line.lower().split()
        cmd = argv[0]

        try:
            if cmd == "help":
                _print_help()
            elif cmd == "pins":
                _print_pins(sbw)
            elif cmd == "status":
                print(format_status(sbw.status()))
            elif cmd == "power-on":
                sbw.power_on()
                print("target power on via GP%d" % _power_pin(sbw))
            elif cmd == "power-off":
                sbw.power_off()
                print("target power off, GP%d returned to input" % _power_pin(sbw))
            elif cmd == "read-jtagid":
                if _require_power(sbw):
                    ok, jtag_id = sbw.read_id()
                    print("jtag-id=0x%02X %s" % (jtag_id, "(expected)" if ok else "(unexpected)"))
            elif cmd == "bypass-test":
                if _require_power(sbw):
                    ok, captured = sbw.bypass_test()
                    print(format_bypass(ok, captured))
            elif cmd == "sync-por":
                if _require_power(sbw):
                    ok, control_capture = sbw.sync_and_por()
                    print(format_sync(ok, control_capture))
            elif cmd == "read-mem16":
                if len(argv) != 2:
                    print("usage: read-mem16 <addr>")
                elif _require_power(sbw):
                    address = _parse_u32(argv[1])
                    ok, value = sbw.read_mem16(address)
                    print("mem[0x%05X]=0x%04X %s" % (address & 0xFFFFF, value, "(read)" if ok else "(unexpected)"))
            elif cmd == "read-block16":
                if len(argv) != 3:
                    print("usage: read-block16 <addr> <words>")
                elif _require_power(sbw):
                    address = _parse_u32(argv[1])
                    words = _parse_u32(argv[2])
                    ok, payload = sbw.read_block16(address, words)
                    print("read-block16 addr=0x%05X words=%d %s" % (address & 0xFFFFF, words, "(read)" if ok else "(unexpected)"))
                    if ok:
                        for index, value in enumerate(bytes_to_words_le(payload)):
                            print("  [%d] = 0x%04X" % (index, value))
            elif cmd == "write-read-mem16":
                if len(argv) != 3:
                    print("usage: write-read-mem16 <addr> <value>")
                elif _require_power(sbw):
                    address = _parse_u32(argv[1])
                    value = _parse_u32(argv[2])
                    ok, readback = sbw.write_mem16(address, value)
                    print("mem[0x%05X]<=0x%04X readback=0x%04X %s" % (
                        address & 0xFFFFF,
                        value & 0xFFFF,
                        readback,
                        "(verified)" if ok else "(unexpected)",
                    ))
            elif cmd == "fram-smoke16":
                if len(argv) != 3:
                    print("usage: fram-smoke16 <addr> <value>")
                elif _require_power(sbw):
                    address = _parse_u32(argv[1])
                    value = _parse_u32(argv[2])
                    ok, original, test_readback, restored_readback = sbw.fram_smoke16(address, value)
                    print("fram[0x%05X] orig=0x%04X test=0x%04X readback=0x%04X restore=0x%04X %s" % (
                        address & 0xFFFFF,
                        original,
                        value & 0xFFFF,
                        test_readback,
                        restored_readback,
                        "(verified-restored)" if ok else "(unexpected)",
                    ))
            elif cmd == "fram-bench":
                if len(argv) != 3:
                    print("usage: fram-bench <addr> <words>")
                elif _require_power(sbw):
                    address = _parse_u32(argv[1])
                    words = _parse_u32(argv[2])
                    ok, result = bench_block_roundtrip(sbw, address, words)
                    word_count, write1_us, read1_us, write2_us, read2_us = result
                    bytes_total = word_count * 2
                    write1_kib = (bytes_total * 1_000_000) // (write1_us * 1024) if write1_us else 0
                    read1_kib = (bytes_total * 1_000_000) // (read1_us * 1024) if read1_us else 0
                    write2_kib = (bytes_total * 1_000_000) // (write2_us * 1024) if write2_us else 0
                    read2_kib = (bytes_total * 1_000_000) // (read2_us * 1024) if read2_us else 0
                    print("fram-bench addr=0x%05X words=%d bytes=%d write1=%dus (%d KiB/s) read1=%dus (%d KiB/s) write2=%dus (%d KiB/s) read2=%dus (%d KiB/s) %s" % (
                        address & 0xFFFFF,
                        word_count,
                        bytes_total,
                        write1_us,
                        write1_kib,
                        read1_us,
                        read1_kib,
                        write2_us,
                        write2_kib,
                        read2_us,
                        read2_kib,
                        "(verified)" if ok else "(unexpected)",
                    ))
            else:
                print("unknown command: %s" % cmd)
        except Exception as exc:
            print("error: %s" % exc)


if __name__ == "__main__":
    repl()
