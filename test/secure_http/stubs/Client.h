#pragma once
#include "Arduino.h"
class IPAddress {
 public:
  uint8_t operator[](size_t) const { return 0; }
};
class Client {
 public:
  virtual ~Client() = default;
  virtual int connect(IPAddress, uint16_t) = 0;
  virtual int connect(const char*, uint16_t) = 0;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t*, size_t) = 0;
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int read(uint8_t*, size_t) = 0;
  virtual int peek() = 0;
  virtual void flush() = 0;
  virtual void stop() = 0;
  virtual uint8_t connected() = 0;
  virtual operator bool() = 0;
  void setTimeout(unsigned long) {}
  unsigned long getTimeout() const { return 1000; }
};
