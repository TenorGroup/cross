#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
#if defined(ARDUINO_USB_MODE) && !ARDUINO_USB_MODE
#include <USBCDC.h>  // Serial is native USB (TinyUSB) CDC
#else
#include <HWCDC.h>
#endif
#endif

#include <string>

/*
Define ENABLE_SERIAL_LOG to enable logging
Can be set in platformio.ini build_flags or as a compile definition

Define LOG_LEVEL to control log verbosity:
0 = ERR only
1 = ERR + INF
2 = ERR + INF + DBG
If not defined, defaults to 0

If you have a legitimate need for raw Serial access (e.g., binary data,
special formatting), use the underlying logSerial object directly:
    logSerial.printf("Special case: %d\n", value);
    logSerial.write(binaryData, length);

The logSerial reference (defined below) points to the real Serial object and
won't trigger deprecation warnings.
*/

#ifndef LOG_LEVEL
#define LOG_LEVEL 0
#endif

#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
#if defined(ARDUINO_USB_MODE) && !ARDUINO_USB_MODE
using LogSerialPort = USBCDC;
#else
using LogSerialPort = HWCDC;
#endif
#define LOG_SERIAL_HAS_TX_TIMEOUT 1
#else
using LogSerialPort = HardwareSerial;
#define LOG_SERIAL_HAS_TX_TIMEOUT 0
#endif

// X4 Pro probe build: the unit under test may have no working serial link, so what the probe
// prints is also kept for the card (src/platform/ColdLog.h) and read back over USB Drive.
#if defined(TENOR_PRESS_PROBE) && FREEINK_DEVICE_X4PRO && !defined(SIMULATOR)
#define TENOR_COLD_LOG 1
#else
#define TENOR_COLD_LOG 0
#endif

#if TENOR_COLD_LOG
void probeLogAppend(const char* data, size_t len);
// Sends to the port and keeps a copy. No base class and a constexpr constructor, so a log line
// from a static constructor finds it ready; members are only built where they are used.
template <typename Port>
class LogSerialTee {
 public:
  constexpr explicit LogSerialTee(Port& port) : port(port) {}
  size_t write(const uint8_t* data, size_t len) {
    probeLogAppend(reinterpret_cast<const char*>(data), len);
    return port.write(data, len);
  }
  size_t write(uint8_t b) { return write(&b, 1); }
  size_t print(const char* text) { return write(reinterpret_cast<const uint8_t*>(text), strlen(text)); }
  size_t printf(const char* format, ...) __attribute__((format(printf, 2, 3))) {
    va_list args, again;
    va_start(args, format);
    va_copy(again, args);
    char line[192];
    const int n = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    size_t sent = 0;
    if (n >= 0 && static_cast<size_t>(n) < sizeof(line)) {
      sent = write(reinterpret_cast<const uint8_t*>(line), n);
    } else if (n > 0) {
      if (char* longer = static_cast<char*>(malloc(n + 1))) {
        vsnprintf(longer, n + 1, format, again);
        sent = write(reinterpret_cast<const uint8_t*>(longer), n);
        free(longer);
      }
    }
    va_end(again);
    return sent;
  }
  int available() { return port.available(); }
  size_t readBytes(uint8_t* buffer, size_t length) { return port.readBytes(buffer, length); }
  String readStringUntil(char end) { return port.readStringUntil(end); }
  void begin(unsigned long baud) { port.begin(baud); }
  size_t setRxBufferSize(size_t size) { return port.setRxBufferSize(size); }
  void end() { port.end(); }
  void setTxTimeoutMs(uint32_t ms) { port.setTxTimeoutMs(ms); }
  void enableReboot(bool on) { port.enableReboot(on); }
  operator bool() const { return static_cast<bool>(port); }

 private:
  Port& port;
};
inline LogSerialTee<LogSerialPort> logSerial{Serial};
// Binary output to the cable (screenshots, file bytes) stays out of the copy, for this task.
struct LogSerialBinary {
  LogSerialBinary();
  ~LogSerialBinary();
};
#else
static LogSerialPort& logSerial = Serial;
struct LogSerialBinary {};
#endif

void logPrintf(const char* level, const char* origin, const char* format, ...);
// The same with the arguments already collected (a library that logs through the app).
void vlogPrintf(const char* level, const char* origin, const char* format, va_list args);

#ifdef ENABLE_SERIAL_LOG
#if LOG_LEVEL >= 0
#define LOG_ERR(origin, format, ...) logPrintf("ERR", origin, format "\n", ##__VA_ARGS__)
#else
#define LOG_ERR(origin, format, ...)
#endif

#if LOG_LEVEL >= 1
#define LOG_INF(origin, format, ...) logPrintf("INF", origin, format "\n", ##__VA_ARGS__)
#else
#define LOG_INF(origin, format, ...)
#endif

#if LOG_LEVEL >= 2
#define LOG_DBG(origin, format, ...) logPrintf("DBG", origin, format "\n", ##__VA_ARGS__)
#else
#define LOG_DBG(origin, format, ...)
#endif
#else
#define LOG_DBG(origin, format, ...)
#define LOG_ERR(origin, format, ...)
#define LOG_INF(origin, format, ...)
#endif

// Measurement lines: info in the press probe build, debug everywhere else, so release builds
// carry neither the call nor its text.
#ifdef TENOR_PRESS_PROBE
#define LOG_PROBE LOG_INF
#else
#define LOG_PROBE LOG_DBG
#endif

#ifdef TENOR_PRESS_PROBE
void probeLogDump();
#endif
#if TENOR_COLD_LOG
// The bytes kept since the last time the card took them, and how many did not fit.
const char* probeLogPending(size_t& len, size_t& dropped);
// The first len bytes are on the card: drop them and the dropped count.
void probeLogConsume(size_t len);
#endif

std::string getLastLogs();
void clearLastLogs();
// Validates the RTC log state (magic word + logHead range). Returns true if
// corruption was detected (magic mismatch or logHead out of range), meaning
// logMessages is untrusted garbage. Callers should call clearLastLogs() when
// this returns true so getLastLogs() does not dump corrupt data into crash reports.
bool sanitizeLogHead();

class MySerialImpl : public Print {
 public:
  void begin(unsigned long baud) { logSerial.begin(baud); }

  // Support boolean conversion for compatibility with code like:
  //   if (Serial) or while (!Serial)
  operator bool() const { return logSerial; }

  __attribute__((deprecated("Use LOG_* macro instead"))) size_t printf(const char* format, ...);
  size_t write(uint8_t b) override;
  size_t write(const uint8_t* buffer, size_t size) override;
  void flush() override;
  static MySerialImpl instance;
};

#ifdef Serial
#undef Serial
#endif
#define Serial MySerialImpl::instance
