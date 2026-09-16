#pragma once
// Toi thieu cho BoardConfig.h tren may ban. Ban that la cua ESP-IDF.
typedef int gpio_num_t;
inline int gpio_hold_dis(gpio_num_t) { return 0; }
inline int gpio_hold_en(gpio_num_t) { return 0; }
