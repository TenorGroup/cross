// A log line never waits on a USB port the host does not drain (lane/TREO-X3.md: the C3's USB
// Serial/JTAG stopped taking IN data; HWCDC::write held the loop and the render task 20 tries x the
// tx timeout per line and the unit stood still). The port here stalls a write that does not fit.
#include <cassert>
#include <chrono>

#include "Logging.h"
#undef Serial
HardwareSerial Serial;
int esp_rom_printf(const char*, ...) { return 0; }

template <typename F>
static long msTaken(F f) {
  const auto start = std::chrono::steady_clock::now();
  f();
  return static_cast<long>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

int main() {
  clearLastLogs();
  Serial.connected = true;

  // The host stopped taking data: no room, and a write would wait 300 ms.
  Serial.room = 0;
  Serial.stallMs = 300;
  const long stuckMs = msTaken([] {
    for (int i = 0; i < 20; ++i) logPrintf("INF", "TEST", "stuck %d\n", i);
  });
  printf("20 lines on a stalled port took %ld ms\n", stuckMs);
  assert(stuckMs < 100);
  assert(Serial.bytes.empty());
  assert(getLastLogs().find("stuck 19") != std::string::npos);  // the crash report still has them

  // The host reads again: the count goes out before the next line.
  Serial.room = 4096;
  logPrintf("INF", "TEST", "back %d\n", 1);
  printf("%s", Serial.bytes.c_str());
  assert(Serial.bytes == "[85] [INF] [LOG] lines dropped: 20\n[85] [INF] [TEST] back 1\n");

  // A port that takes nothing (TinyUSB's write timed out) loses the line: counted too.
  Serial.bytes.clear();
  Serial.refuse = true;
  logPrintf("INF", "TEST", "refused\n");
  Serial.refuse = false;
  logPrintf("INF", "TEST", "after\n");
  assert(Serial.bytes == "[85] [INF] [LOG] lines dropped: 1\n[85] [INF] [TEST] after\n");

  // No host, or a SOF flap at the 10 MHz clock: the line is still written (the port queues without
  // waiting and re-arms its TX interrupt), with no room asked.
  Serial.bytes.clear();
  Serial.connected = false;
  Serial.room = 0;
  logPrintf("INF", "TEST", "flap\n");
  assert(Serial.bytes == "[85] [INF] [TEST] flap\n");

  // CMD:TXTIMEOUT <ms> (probe) is the whole wait for a host that stopped reading, on either port:
  // HWCDC waits its timeout once per try, 20 tries, so 200 asked must set 10 (4 s before).
#if LOG_SERIAL_HWCDC
  assert(logSerialTimeoutFor(200) == 10 && logSerialTimeoutFor(5) == 1);
#else
  assert(logSerialTimeoutFor(200) == 200);
#endif

  puts("stalled port: lines dropped at once and counted, the count sent when it drains");
}
