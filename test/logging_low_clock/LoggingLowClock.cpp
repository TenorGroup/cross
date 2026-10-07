#include <cassert>
#include "Logging.h"
#undef Serial
HardwareSerial Serial;
static unsigned romWrites;
static std::string romBytes;
int esp_rom_printf(const char* format, ...) {
  char bytes[256];
  va_list args;
  va_start(args, format);
  const int count = vsnprintf(bytes, sizeof(bytes), format, args);
  va_end(args);
  ++romWrites;
  romBytes += bytes;
  return count;
}

int main() {
  clearLastLogs();
  logPrintf("INF", "TEST", "idle %d\n", 1);
  Serial.connected = true;
  logPrintf("INF", "TEST", "awake %d\n", 2);
  Serial.connected = false;
  logPrintf("ERR", "TEST", "no host %d\n", 3);
  const std::string expected = "[85] [INF] [TEST] idle 1\n[85] [INF] [TEST] awake 2\n[85] [ERR] [TEST] no host 3\n";
#if FREEINK_LOG_TRANSPORT == FREEINK_LOG_TRANSPORT_ROM_PRINTF
  assert(romWrites == 3 && romBytes == expected);
  assert(Serial.writes == 0);
#else
  assert(Serial.writes == 3 && Serial.bytes == expected);
  assert(romWrites == 0);
#endif
  assert(getLastLogs() == expected);
  puts("3/3 formatted lines delivered, ring preserved");
}
