#include "sbw_hw.h"

#include "hardware/gpio.h"
#include "hardware/structs/sio.h"
#include "pico/stdlib.h"

#include "sbw_pins.h"

enum {
    SBW_CLOCK_MASK = 1u << SBW_PIN_CLOCK,
    SBW_DATA_MASK = 1u << SBW_PIN_DATA,
    SBW_TARGET_POWER_MASK = 1u << SBW_PIN_TARGET_POWER,
};

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
        sio_hw->gpio_set = SBW_TARGET_POWER_MASK;
        sio_hw->gpio_oe_set = SBW_TARGET_POWER_MASK;
        g_target_power_enabled = true;
        sleep_ms(SBW_TARGET_POWER_SETTLE_MS);
        return;
    }

    sio_hw->gpio_oe_clr = SBW_TARGET_POWER_MASK;
    sbw_disable_pulls(SBW_PIN_TARGET_POWER);
    g_target_power_enabled = false;
}

bool sbw_hw_target_power_enabled(void) {
    return g_target_power_enabled;
}

void sbw_hw_clock_drive(bool high) {
    if (high) {
        sio_hw->gpio_set = SBW_CLOCK_MASK;
    } else {
        sio_hw->gpio_clr = SBW_CLOCK_MASK;
    }
    g_clock_is_high = high;
}

bool sbw_hw_clock_is_high(void) {
    return g_clock_is_high;
}

void sbw_hw_data_drive(bool level) {
    if (level) {
        sio_hw->gpio_set = SBW_DATA_MASK;
    } else {
        sio_hw->gpio_clr = SBW_DATA_MASK;
    }

    if (!g_data_is_driving) {
        sio_hw->gpio_oe_set = SBW_DATA_MASK;
        g_data_is_driving = true;
    }
}

void sbw_hw_data_release(void) {
    if (g_data_is_driving) {
        sio_hw->gpio_oe_clr = SBW_DATA_MASK;
        g_data_is_driving = false;
    }
}

bool sbw_hw_data_is_driving(void) {
    return g_data_is_driving;
}

bool sbw_hw_data_read(void) {
    return (sio_hw->gpio_in & SBW_DATA_MASK) != 0;
}
