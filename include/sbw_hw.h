#pragma once

#include <stdbool.h>
#include <stdint.h>

void sbw_hw_init(void);

void sbw_hw_target_power_set(bool enabled);
bool sbw_hw_target_power_enabled(void);

void sbw_hw_clock_drive(bool high);
void sbw_hw_clock_pulse_us(uint32_t low_us, uint32_t high_us);
bool sbw_hw_clock_is_high(void);

void sbw_hw_data_drive(bool level);
void sbw_hw_data_release(void);
bool sbw_hw_data_is_driving(void);
bool sbw_hw_data_read(void);
