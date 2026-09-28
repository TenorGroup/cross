#pragma once
#include <cstdint>
#include <driver/gpio.h>
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_SLEEP_WAKEUP_ALL = 0;
constexpr int ESP_GPIO_WAKEUP_GPIO_LOW = 0, ESP_GPIO_WAKEUP_GPIO_HIGH = 1;
constexpr int ESP_EXT1_WAKEUP_ANY_LOW = 0, ESP_EXT1_WAKEUP_ANY_HIGH = 1;
using esp_sleep_ext1_wakeup_mode_t = int;
int esp_sleep_disable_wakeup_source(int);
int esp_sleep_enable_timer_wakeup(uint64_t);
int esp_light_sleep_start();
int esp_deep_sleep_enable_gpio_wakeup(uint64_t, int);
int esp_sleep_enable_ext1_wakeup(uint64_t, int);
void esp_sleep_config_gpio_isolate();
[[noreturn]] void esp_deep_sleep_start();
// Only read on the reset path after a rejected sleep entry, which the harness never takes.
inline int esp_sleep_get_wakeup_cause() { return 0; }
