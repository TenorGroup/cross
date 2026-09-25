// What SdFat's library code needs from Arduino when built for the host test.
#pragma once
#include <stdint.h>
struct __FlashStringHelper;
inline uint32_t millis() { return 0; }
