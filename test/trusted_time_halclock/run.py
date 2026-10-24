"""TrustedTime against host stubs: HalClock is the only SNTP owner, the NVS floor keeps its guards (D12)."""

from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

STUBS = {
    "Arduino.h": r'''
#pragma once
#include <cstdlib>
#include <ctime>
#include <sys/time.h>
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
''',
    "Preferences.h": r'''
#pragma once
#include <cstdint>
// Stands in for the NVS floor: keeps the last written value so init() and adopt() are observable.
inline int64_t storedFloor = 0;
struct Preferences {
  bool begin(const char*, bool) { return true; }
  int64_t getLong64(const char*, int64_t value) { return value ? value : storedFloor; }
  void putLong64(const char*, int64_t v) { storedFloor = v; }
  void end() {}
};
''',
    "Logging.h": '#pragma once\n#define LOG_DBG(...) ((void)0)\n',
    # The only door to SNTP. A configTzTime or esp_sntp.h in TrustedTime would no longer compile.
    "HalClock.h": r'''
#pragma once
struct HalClockStub {
  int syncs = 0;
  bool answer = true;
  bool syncFromNTP() {
    ++syncs;
    return answer;
  }
};
inline HalClockStub halClock;
''',
}

CHECK = r'''
#include <TrustedTime.h>
#include <Arduino.h>
#include <HalClock.h>
#include <Preferences.h>
#include <cassert>
#include <cstring>
int main() {
  const int64_t host = static_cast<int64_t>(time(nullptr));
  assert(host > 1735689600LL && host < 4102444800LL - 86400LL * 400);  // the checks below need a plausible host date

  // No RTC, or an unset chip (2000-01-01): nothing is adopted.
  trustedtime::adopt(0);
  trustedtime::adopt(946684800);
  assert(storedFloor == 0);

  // RTC year 2099 with no floor on record: a faulty chip, not a date. It must not become a floor nothing can lower.
  trustedtime::adopt(4070908800LL);  // 2099-01-01
  assert(storedFloor == 0);
  trustedtime::adopt(4102444801LL);  // past the supported window
  assert(storedFloor == 0);

  // A believable RTC is taken and persisted.
  const int64_t rtcNow = host + 3600;
  trustedtime::adopt(rtcNow);
  assert(storedFloor == rtcNow);
  // A lagging RTC does not move the floor; one a year ahead of it (still inside the window) is faulty.
  trustedtime::adopt(rtcNow - 86400);
  assert(storedFloor == rtcNow);
  trustedtime::adopt(rtcNow + 400 * 86400LL);
  assert(storedFloor == rtcNow);
  trustedtime::adopt(4070908800LL);  // 2099 again, now with a floor
  assert(storedFloor == rtcNow);
  trustedtime::adopt(rtcNow + 3600);
  assert(storedFloor == rtcNow + 3600);

  // HalClock is the only SNTP owner: TrustedTime never touches the time zone the display depends on.
  const char* zone = "EST5EDT,M3.2.0,M11.1.0";
  setenv("TZ", zone, 1);
  tzset();

  // The clock came from a real source (the RTC): joining a network asks HalClock for nothing.
  assert(!trustedtime::clockNeedsSync());
  trustedtime::startSync();
  assert(halClock.syncs == 0);

  // A cold boot with no RTC leaves the clock on the NVS floor: it may lag the wall clock.
  storedFloor = host + 30 * 86400LL;  // a floor ahead of the host clock stands in for a clock behind the floor
  trustedtime::init();
  assert(trustedtime::clockNeedsSync());
  halClock.answer = false;
  trustedtime::startSync();  // the join cannot get the time: still floor only, one attempt
  assert(halClock.syncs == 1 && trustedtime::clockNeedsSync());
  halClock.answer = true;
  trustedtime::startSync();
  assert(halClock.syncs == 2 && !trustedtime::clockNeedsSync());
  trustedtime::startSync();  // synced: the next join asks nothing
  assert(halClock.syncs == 2);

  // syncNow goes through HalClock too and reports its answer.
  halClock.answer = false;
  assert(!trustedtime::syncNow(200) && halClock.syncs == 3);
  halClock.answer = true;
  assert(trustedtime::syncNow(200) && halClock.syncs == 4);

  assert(strcmp(getenv("TZ"), zone) == 0);
  unsetenv("TZ");
}
'''

if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="trusted-time-halclock-") as directory:
        work = Path(directory)
        for name, content in STUBS.items():
            (work / name).write_text(content)
        (work / "check.cpp").write_text(CHECK)
        subprocess.run([
            "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            f"-I{work}", f"-I{ROOT / 'lib/TrustedTime'}",
            str(ROOT / "lib/TrustedTime/TrustedTime.cpp"), str(work / "check.cpp"),
            "-o", str(work / "check"),
        ], check=True)
        subprocess.run([str(work / "check")], check=True)
    print("TrustedTime floor and HalClock checks passed")
