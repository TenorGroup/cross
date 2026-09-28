#pragma once
#include <cstdint>
struct FakeClock { bool getDateTime(uint16_t&, uint8_t&, uint8_t&, uint8_t&, uint8_t&){return false;} bool utcOffsetMinutes(int&) const {return false;} };
inline FakeClock halClock;
