#include "sbw_hw.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include "sbw_pins.h"

static bool g_target_power_enabled;
static bool g_data_is_driving;
static bool g_clock_is_high;

static void sbw_disable_pulls(uint pin) {
    gpio_disable_pulls(pin);
}

void sbw_hw_init(void) {
    gpio_init(SBW_PIN_CLOCK);
    gpio_set_dir(SBW_PIN_CLOCK, GPIO_OUT);
    gpio_put(SBW_PIN_CLOCK, 1);
    sbw_disable_pulls(SBW_PIN_CLOCK);
    g_clock_is_high = true;

    gpio_init(SBW_PIN_DATA);
    gpio_set_dir(SBW_PIN_DATA, GPIO_IN);
    sbw_disable_pulls(SBW_PIN_DATA);
    g_data_is_driving = false;

    gpio_init(SBW_PIN_TARGET_POWER);
    gpio_set_dir(SBW_PIN_TARGET_POWER, GPIO_IN);
    sbw_disable_pulls(SBW_PIN_TARGET_POWER);
    g_target_power_enabled = false;
}

void sbw_hw_target_power_set(bool enabled) {
    if (enabled) {
        gpio_put(SBW_PIN_TARGET_POWER, 1);
        gpio_set_dir(SBW_PIN_TARGET_POWER, GPIO_OUT);
        g_target_power_enabled = true;
        sleep_ms(SBW_TARGET_POWER_SETTLE_MS);
        return;
    }

    gpio_set_dir(SBW_PIN_TARGET_POWER, GPIO_IN);
    sbw_disable_pulls(SBW_PIN_TARGET_POWER);
    g_target_power_enabled = false;
}

bool sbw_hw_target_power_enabled(void) {
    return g_target_power_enabled;
}

void sbw_hw_clock_drive(bool high) {
    gpio_put(SBW_PIN_CLOCK, high ? 1 : 0);
    g_clock_is_high = high;
}

void sbw_hw_clock_pulse_us(uint32_t low_us, uint32_t high_us) {
    sbw_hw_clock_drive(false);
    busy_wait_us_32(low_us);
    sbw_hw_clock_drive(true);
    busy_wait_us_32(high_us);
}

bool sbw_hw_clock_is_high(void) {
    return g_clock_is_high;
}

void sbw_hw_data_drive(bool level) {
    gpio_put(SBW_PIN_DATA, level);
    gpio_set_dir(SBW_PIN_DATA, GPIO_OUT);
    g_data_is_driving = true;
}

void sbw_hw_data_release(void) {
    gpio_set_dir(SBW_PIN_DATA, GPIO_IN);
    sbw_disable_pulls(SBW_PIN_DATA);
    g_data_is_driving = false;
}

bool sbw_hw_data_is_driving(void) {
    return g_data_is_driving;
}

bool sbw_hw_data_read(void) {
    return gpio_get(SBW_PIN_DATA);
}
