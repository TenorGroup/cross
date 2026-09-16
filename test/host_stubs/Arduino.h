#pragma once
#include <cstdint>
#include <cstddef>
#include <Print.h>
#include <HardwareSerial.h>
#include <WString.h>
enum { LOW = 0, HIGH = 1, INPUT = 0, OUTPUT = 1, INPUT_PULLUP = 2 };
unsigned long millis();
void delay(unsigned long);
void pinMode(uint8_t, uint8_t);
void digitalWrite(uint8_t, uint8_t);
int digitalRead(uint8_t);
#include <cassert>
// Doi tuong ESP cua Arduino-ESP32: chi nhung loi goi ma ma nguon that dung toi.
struct EspClass {
  void restart() {}
  uint32_t getFreeHeap() { return 200000; }
  uint32_t getMinFreeHeap() { return 150000; }
};
extern EspClass ESP;
