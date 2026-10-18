#pragma once

#include <HwLog.h>

#include <cstdint>
#include <string>

#define LOW 0x0
#define HIGH 0x1
#define INPUT 0x01
#define OUTPUT 0x03

inline void pinMode(uint8_t pin, uint8_t mode) {
  hwLog.push_back("mode " + std::to_string(pin) + (mode == OUTPUT ? " OUTPUT" : " INPUT"));
}
inline void digitalWrite(uint8_t pin, uint8_t level) {
  hwLog.push_back("write " + std::to_string(pin) + (level ? " HIGH" : " LOW"));
}
inline void delay(uint32_t ms) { hwLog.push_back("delay " + std::to_string(ms)); }
