#pragma once
struct HalClockStub {
  bool syncFromNTP() { return false; }
};
inline HalClockStub halClock;
