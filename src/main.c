#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"

#include "sbw_hw.h"
#include "sbw_jtag.h"
#include "sbw_pins.h"
#include "sbw_transport.h"

static const sbw_timing_t k_default_timing = {
    .clock_low_us = SBW_DEFAULT_CLOCK_LOW_US,
    .clock_high_us = SBW_DEFAULT_CLOCK_HIGH_US,
    .sample_delay_us = SBW_DEFAULT_SAMPLE_DELAY_US,
};

static bool ensure_target_powered(void);

static bool read_line(char *buffer, size_t size) {
    static size_t used = 0;

    while (true) {
        const int ch = getchar_timeout_us(0);
        if (ch == PICO_ERROR_TIMEOUT) {
            return false;
        }

        if (ch == '\r' || ch == '\n') {
            if (used == 0) {
                continue;
            }

            buffer[used] = '\0';
            used = 0;
            return true;
        }

        if ((ch == '\b' || ch == 127) && used > 0) {
            --used;
            continue;
        }

        if (ch >= 32 && ch <= 126 && used + 1 < size) {
            buffer[used++] = (char)ch;
        }
    }
}

static void print_help(void) {
    printf("Commands:\n");
    printf("  help\n");
    printf("  pins\n");
    printf("  status\n");
    printf("  power-on\n");
    printf("  power-off\n");
    printf("  release\n");
    printf("  tap-reset\n");
    printf("  read-jtagid\n");
    printf("  bypass-test\n");
    printf("  sync-por\n");
    printf("  read-mem16 <addr>\n");
    printf("  write-read-mem16 <addr> <value>\n");
    printf("  clock-test [cycles] [low_us] [high_us]\n");
    printf("  slot-test <tms:0|1> <tdi:0|1> [low_us] [high_us] [sample_us]\n");
}

static void print_pins(void) {
    printf("SBWTCK  GP%d\n", SBW_PIN_CLOCK);
    printf("SBWTDIO GP%d\n", SBW_PIN_DATA);
    printf("VCC     GP%d\n", SBW_PIN_TARGET_POWER);
    printf("GND     Pico GND\n");
}

static void print_status(void) {
    printf("power=%s data=%s clock=%s\n",
        sbw_hw_target_power_enabled() ? "on" : "off",
        sbw_hw_data_is_driving() ? "driving" : "input",
        sbw_hw_clock_is_high() ? "high" : "low");
}

static bool parse_u32(const char *token, uint32_t *value) {
    if (!token || !value) {
        return false;
    }

    char *end = NULL;
    const unsigned long parsed = strtoul(token, &end, 0);
    if (*token == '\0' || !end || *end != '\0') {
        return false;
    }

    *value = (uint32_t)parsed;
    return true;
}

static bool parse_bit(const char *token, bool *value) {
    uint32_t parsed = 0;
    if (!parse_u32(token, &parsed) || parsed > 1) {
        return false;
    }

    *value = (parsed != 0);
    return true;
}

static void lowercase(char *text) {
    for (; *text; ++text) {
        *text = (char)tolower((unsigned char)*text);
    }
}

static void handle_clock_test(char *arg1, char *arg2, char *arg3) {
    uint32_t cycles = 16;
    uint32_t low_us = SBW_DEFAULT_CLOCK_LOW_US;
    uint32_t high_us = SBW_DEFAULT_CLOCK_HIGH_US;

    if ((arg1 && !parse_u32(arg1, &cycles)) ||
        (arg2 && !parse_u32(arg2, &low_us)) ||
        (arg3 && !parse_u32(arg3, &high_us))) {
        printf("usage: clock-test [cycles] [low_us] [high_us]\n");
        return;
    }

    if (!ensure_target_powered()) {
        return;
    }

    printf("clock-test cycles=%lu low=%luus high=%luus\n",
        (unsigned long)cycles,
        (unsigned long)low_us,
        (unsigned long)high_us);

    sbw_transport_clock_test(cycles, low_us, high_us);
    printf("clock-test complete\n");
}

static void handle_slot_test(char *arg1, char *arg2, char *arg3, char *arg4, char *arg5) {
    bool tms = false;
    bool tdi = false;
    sbw_timing_t timing = k_default_timing;

    if (!parse_bit(arg1, &tms) || !parse_bit(arg2, &tdi)) {
        printf("usage: slot-test <tms:0|1> <tdi:0|1> [low_us] [high_us] [sample_us]\n");
        return;
    }

    if ((arg3 && !parse_u32(arg3, &timing.clock_low_us)) ||
        (arg4 && !parse_u32(arg4, &timing.clock_high_us)) ||
        (arg5 && !parse_u32(arg5, &timing.sample_delay_us))) {
        printf("usage: slot-test <tms:0|1> <tdi:0|1> [low_us] [high_us] [sample_us]\n");
        return;
    }

    if (!ensure_target_powered()) {
        return;
    }

    printf("slot-test tms=%u tdi=%u low=%luus high=%luus sample=%luus\n",
        tms ? 1u : 0u,
        tdi ? 1u : 0u,
        (unsigned long)timing.clock_low_us,
        (unsigned long)timing.clock_high_us,
        (unsigned long)timing.sample_delay_us);

    const bool tdo = sbw_transport_io_bit(tms, tdi, &timing);
    sbw_transport_release();

    printf("slot-test tdo=%u\n", tdo ? 1u : 0u);
}

static bool ensure_target_powered(void) {
    if (sbw_hw_target_power_enabled()) {
        return true;
    }

    printf("target power is off\n");
    return false;
}

static void handle_tap_reset(void) {
    if (!ensure_target_powered()) {
        return;
    }

    sbw_transport_start();
    sbw_jtag_tap_reset();
    sbw_transport_release();
    printf("tap reset complete\n");
}

static void handle_read_jtagid(void) {
    uint8_t id = 0;

    if (!ensure_target_powered()) {
        return;
    }

    const bool ok = sbw_jtag_read_id(&id);
    printf("jtag-id=0x%02X %s\n", id, ok ? "(expected)" : "(unexpected)");
}

static void handle_bypass_test(void) {
    uint16_t captured = 0;

    if (!ensure_target_powered()) {
        return;
    }

    const bool ok = sbw_jtag_bypass_test(&captured);
    printf("bypass pattern=0x%04X captured=0x%04X expected=0x%04X %s\n",
        SBW_BYPASS_SMOKE_PATTERN,
        captured,
        SBW_BYPASS_SMOKE_EXPECTED,
        ok ? "(expected)" : "(unexpected)");
}

static void handle_sync_por(void) {
    uint16_t control_capture = 0;

    if (!ensure_target_powered()) {
        return;
    }

    const bool ok = sbw_jtag_sync_and_por(&control_capture);
    printf("cntrl-sig=0x%04X %s\n",
        control_capture,
        ok ? "(full-emulation)" : "(unexpected)");
}

static void handle_read_mem16(char *arg1) {
    uint32_t address = 0;
    uint16_t value = 0;

    if (!parse_u32(arg1, &address)) {
        printf("usage: read-mem16 <addr>\n");
        return;
    }

    if (!ensure_target_powered()) {
        return;
    }

    const bool ok = sbw_jtag_read_mem16(address, &value);
    printf("mem[0x%05lX]=0x%04X %s\n",
        (unsigned long)(address & 0x000FFFFFul),
        value,
        ok ? "(read)" : "(unexpected)");
}

static void handle_write_read_mem16(char *arg1, char *arg2) {
    uint32_t address = 0;
    uint32_t value32 = 0;
    uint16_t readback = 0;

    if (!parse_u32(arg1, &address) || !parse_u32(arg2, &value32) || value32 > 0xFFFFu) {
        printf("usage: write-read-mem16 <addr> <value>\n");
        return;
    }

    if (!ensure_target_powered()) {
        return;
    }

    const bool ok = sbw_jtag_write_mem16(address, (uint16_t)value32, &readback);
    printf("mem[0x%05lX]<=0x%04lX readback=0x%04X %s\n",
        (unsigned long)(address & 0x000FFFFFul),
        (unsigned long)value32,
        readback,
        ok ? "(verified)" : "(unexpected)");
}

static void handle_command(char *line) {
    char *argv[6] = {0};
    size_t argc = 0;

    for (char *token = strtok(line, " \t"); token && argc < 6; token = strtok(NULL, " \t")) {
        lowercase(token);
        argv[argc++] = token;
    }

    if (argc == 0) {
        return;
    }

    if (strcmp(argv[0], "help") == 0) {
        print_help();
        return;
    }

    if (strcmp(argv[0], "pins") == 0) {
        print_pins();
        return;
    }

    if (strcmp(argv[0], "status") == 0) {
        print_status();
        return;
    }

    if (strcmp(argv[0], "power-on") == 0) {
        sbw_transport_release();
        sbw_hw_target_power_set(true);
        printf("target power on via GP%d\n", SBW_PIN_TARGET_POWER);
        return;
    }

    if (strcmp(argv[0], "power-off") == 0) {
        sbw_transport_release();
        sbw_hw_target_power_set(false);
        printf("target power off, GP%d returned to input\n", SBW_PIN_TARGET_POWER);
        return;
    }

    if (strcmp(argv[0], "release") == 0) {
        sbw_transport_release();
        printf("transport released, SBWTDIO back to input\n");
        return;
    }

    if (strcmp(argv[0], "tap-reset") == 0) {
        handle_tap_reset();
        return;
    }

    if (strcmp(argv[0], "read-jtagid") == 0) {
        handle_read_jtagid();
        return;
    }

    if (strcmp(argv[0], "bypass-test") == 0) {
        handle_bypass_test();
        return;
    }

    if (strcmp(argv[0], "sync-por") == 0) {
        handle_sync_por();
        return;
    }

    if (strcmp(argv[0], "read-mem16") == 0) {
        handle_read_mem16(argv[1]);
        return;
    }

    if (strcmp(argv[0], "write-read-mem16") == 0) {
        handle_write_read_mem16(argv[1], argv[2]);
        return;
    }

    if (strcmp(argv[0], "clock-test") == 0) {
        handle_clock_test(argv[1], argv[2], argv[3]);
        return;
    }

    if (strcmp(argv[0], "slot-test") == 0) {
        handle_slot_test(argv[1], argv[2], argv[3], argv[4], argv[5]);
        return;
    }

    printf("unknown command: %s\n", argv[0]);
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000);

    sbw_hw_init();
    sbw_transport_init();

    printf("\npi-pico-sbw bring-up shell\n");
    print_pins();
    print_help();

    char line[96];

    while (true) {
        if (read_line(line, sizeof(line))) {
            handle_command(line);
        }

        tight_loop_contents();
    }
}
