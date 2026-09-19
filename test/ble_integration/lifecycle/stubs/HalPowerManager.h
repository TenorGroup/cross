#pragma once
#include <stdexcept>
namespace power_test {
inline unsigned locks = 0;
inline unsigned maximum = 0;
inline bool saving = false;
inline unsigned enableRequests = 0;
}
class HalPowerManager {
 public:
  static constexpr unsigned long IDLE_POWER_SAVING_MS = 3000;
  void setPowerSaving(bool enabled) { if (enabled) ++power_test::enableRequests; power_test::saving = enabled && power_test::locks == 0; }
  class Lock {
   public:
    Lock() { power_test::saving = false; ++power_test::locks; if (power_test::locks > power_test::maximum) power_test::maximum = power_test::locks; }
    ~Lock() { if (!power_test::locks) std::terminate(); --power_test::locks; }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
  };
};
