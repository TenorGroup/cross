#pragma once
#include <driver/gpio.h>
// The pad pulls the RTC domain applies during ext1 sleep; recorded by fakes.inc like every other pin call.
int rtc_gpio_pulldown_dis(gpio_num_t);
int rtc_gpio_pullup_en(gpio_num_t);
int rtc_gpio_pullup_dis(gpio_num_t);
int rtc_gpio_pulldown_en(gpio_num_t);
