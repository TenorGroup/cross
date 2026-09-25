#pragma once
struct HalClock {
  bool syncFromNTP() { return true; }
};
inline HalClock halClock;
