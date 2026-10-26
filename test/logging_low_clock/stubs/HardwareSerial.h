#pragma once
#include <chrono>
#include <functional>
#include <thread>

#include "Arduino.h"
class HardwareSerial {
 public:
  bool connected = false;
  unsigned writes = 0;
  std::string bytes;
  size_t room = 4096;    // free bytes in the TX buffer; the host drains what it is given at once
  int stallMs = 0;       // a write that does not fit waits this long, then takes nothing (HWCDC: 20 tries)
  bool refuse = false;   // the port takes nothing (TinyUSB's write timed out)
  uint32_t timeoutMs = 1;                       // setTxTimeoutMs
  std::function<void(const std::string&)> onWrite;  // runs after a write took its bytes: another task logging
  operator bool() const { return connected; }
  int availableForWrite() { return static_cast<int>(room); }
  size_t print(const char* text) { return write(reinterpret_cast<const uint8_t*>(text), strlen(text)); }
  size_t write(const uint8_t* data, size_t len) {
    ++writes;
    if (connected && refuse) return 0;
    if (connected && len > room) {
      std::this_thread::sleep_for(std::chrono::milliseconds(stallMs));
      return 0;
    }
    bytes.append(reinterpret_cast<const char*>(data), len);  // with no host the port queues, never waits
    if (onWrite) {
      auto hook = std::move(onWrite);
      onWrite = nullptr;
      hook(std::string(reinterpret_cast<const char*>(data), len));
    }
    return len;
  }
  size_t printf(const char* format, ...) {
    char text[256];
    va_list args;
    va_start(args, format);
    const int count = vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (count < 0) return 0;
    return print(text);
  }
  void begin(unsigned long) {}
  void setTxTimeoutMs(uint32_t ms) { timeoutMs = ms; }
};
extern HardwareSerial Serial;
