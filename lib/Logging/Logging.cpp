#include "Logging.h"

#include <BoardConfig.h>
#include <esp_rom_sys.h>

#include <string>

#define MAX_ENTRY_LEN 256
#define MAX_LOG_LINES 16

// Simple ring buffer log, useful for error reporting when we encounter a crash
RTC_NOINIT_ATTR char logMessages[MAX_LOG_LINES][MAX_ENTRY_LEN];
RTC_NOINIT_ATTR size_t logHead = 0;
// Magic word written alongside logHead to detect uninitialized RTC memory.
// RTC_NOINIT_ATTR is not zeroed on cold boot, so logHead may appear in-range
// (0..MAX_LOG_LINES-1) by chance even though logMessages is garbage. The magic
// value is only set by clearLastLogs(), so its absence means the buffer was
// never properly initialized.
RTC_NOINIT_ATTR uint32_t rtcLogMagic;
static constexpr uint32_t LOG_RTC_MAGIC = 0xDEADBEEF;

#ifdef TENOR_PRESS_PROBE
// Probe builds keep a RAM copy of the log from boot, so a wake measured over a cable that
// only reconnects after USB enumerates still has its earliest lines (CMD:LOGDUMP).
#if TENOR_COLD_LOG
// Everything sent to the cable lands here and goes to the card after each script step.
static char probeLog[32768];
static size_t probeLogDropped = 0;
static portMUX_TYPE probeLogLock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t probeLogBinaryTask = nullptr;
#else
static char probeLog[6144];
#endif
static size_t probeLogLen = 0;
#if TENOR_COLD_LOG
void probeLogAppend(const char* data, const size_t n) {
  if (probeLogBinaryTask && probeLogBinaryTask == xTaskGetCurrentTaskHandle()) return;
  portENTER_CRITICAL_SAFE(&probeLogLock);
  if (probeLogLen + n > sizeof(probeLog)) {
    probeLogDropped += n;
  } else {
    memcpy(probeLog + probeLogLen, data, n);
    probeLogLen += n;
  }
  portEXIT_CRITICAL_SAFE(&probeLogLock);
}
const char* probeLogPending(size_t& len, size_t& dropped) {
  portENTER_CRITICAL(&probeLogLock);
  len = probeLogLen;
  dropped = probeLogDropped;
  portEXIT_CRITICAL(&probeLogLock);
  return probeLog;
}
void probeLogConsume(size_t len) {
  portENTER_CRITICAL(&probeLogLock);
  len = std::min(len, probeLogLen);
  memmove(probeLog, probeLog + len, probeLogLen - len);
  probeLogLen -= len;
  probeLogDropped = 0;
  portEXIT_CRITICAL(&probeLogLock);
}
LogSerialBinary::LogSerialBinary() { probeLogBinaryTask = xTaskGetCurrentTaskHandle(); }
LogSerialBinary::~LogSerialBinary() { probeLogBinaryTask = nullptr; }
#else
static void probeLogAppend(const char* line) {
  const size_t n = strlen(line);
  if (probeLogLen + n > sizeof(probeLog)) return;
  memcpy(probeLog + probeLogLen, line, n);
  probeLogLen += n;
}
#endif
void probeLogDump() {
  [[maybe_unused]] LogSerialBinary binary;
  logSerial.printf("LOGDUMP_START:%u\n", static_cast<unsigned>(probeLogLen));
  logSerial.write(reinterpret_cast<const uint8_t*>(probeLog), probeLogLen);
  logSerial.printf("LOGDUMP_END\n");
#if !TENOR_COLD_LOG
  probeLogLen = 0;  // the cold log keeps them for the card
#endif
}
#endif

void addToLogRingBuffer(const char* message) {
  // Add the message to the ring buffer, overwriting old messages if necessary.
  // If the magic is wrong or logHead is out of range (RTC_NOINIT_ATTR garbage
  // on cold boot), clear the entire buffer so subsequent reads are safe.
  if (rtcLogMagic != LOG_RTC_MAGIC || logHead >= MAX_LOG_LINES) {
    memset(logMessages, 0, sizeof(logMessages));
    logHead = 0;
    rtcLogMagic = LOG_RTC_MAGIC;
  }
  strncpy(logMessages[logHead], message, MAX_ENTRY_LEN - 1);
  logMessages[logHead][MAX_ENTRY_LEN - 1] = '\0';
  logHead = (logHead + 1) % MAX_LOG_LINES;
}

// Since logging can take a large amount of flash, we want to make the format string as short as possible.
// This logPrintf prepend the timestamp, level and origin to the user-provided message, so that the user only needs to
// provide the format string for the message itself.
void logPrintf(const char* level, const char* origin, const char* format, ...) {
  va_list args;
  va_start(args, format);
  vlogPrintf(level, origin, format, args);
  va_end(args);
}

void vlogPrintf(const char* level, const char* origin, const char* format, va_list args) {
  char buf[MAX_ENTRY_LEN];
  char* c = buf;
  // add timestamp, level and origin
  {
    unsigned long ms = millis();
    int len = snprintf(c, sizeof(buf), "[%lu] [%s] [%s] ", ms, level, origin);
    // error while writing => return
    if (len < 0) {
      return;
    }
    // clamp c to be in buffer range
    c += std::min(len, MAX_ENTRY_LEN);
  }
  // add the user message
  {
    int len = vsnprintf(c, sizeof(buf) - (c - buf), format, args);
    if (len < 0) {
      return;
    }
  }
#if FREEINK_LOG_TRANSPORT == FREEINK_LOG_TRANSPORT_ROM_PRINTF
  // Sticky's USB serial bridge uses UART0; ROM output also works before Serial0.begin().
  esp_rom_printf("%s", buf);
#elif TENOR_COLD_LOG
  logSerial.print(buf);  // the tee keeps the copy, with or without a host on the port
#else
  if (logSerial) {
    logSerial.print(buf);
  }
#endif
  addToLogRingBuffer(buf);
#if defined(TENOR_PRESS_PROBE) && !TENOR_COLD_LOG
  probeLogAppend(buf);
#endif
}

std::string getLastLogs() {
  if (rtcLogMagic != LOG_RTC_MAGIC) {
    return {};
  }
  std::string output;
  for (size_t i = 0; i < MAX_LOG_LINES; i++) {
    size_t idx = (logHead + i) % MAX_LOG_LINES;
    if (logMessages[idx][0] != '\0') {
      const size_t len = strnlen(logMessages[idx], MAX_ENTRY_LEN);
      output.append(logMessages[idx], len);
    }
  }
  return output;
}

// Checks whether the RTC log state is consistent: rtcLogMagic must equal
// LOG_RTC_MAGIC and logHead must be in 0..MAX_LOG_LINES-1. Returns true if
// corruption is detected, in which case rtcLogMagic is still invalid and
// logMessages may contain garbage. Callers (e.g. HalSystem::begin on the
// panic-reboot path) must call clearLastLogs() after a true result to fully
// reinitialize the ring buffer and stamp the magic before getLastLogs() is used.
bool sanitizeLogHead() {
  if (rtcLogMagic != LOG_RTC_MAGIC || logHead >= MAX_LOG_LINES) {
    logHead = 0;
    return true;
  }
  return false;
}

void clearLastLogs() {
  for (size_t i = 0; i < MAX_LOG_LINES; i++) {
    logMessages[i][0] = '\0';
  }
  logHead = 0;
  rtcLogMagic = LOG_RTC_MAGIC;
}
