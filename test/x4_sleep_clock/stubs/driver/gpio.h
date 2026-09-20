#pragma once
using gpio_num_t = int;
constexpr int GPIO_NUM_13 = 13, GPIO_MODE_OUTPUT = 1;
void gpio_hold_dis(int);
void gpio_hold_en(int);
void gpio_set_direction(int, int);
void gpio_set_level(int, int);
void gpio_deep_sleep_hold_en();
