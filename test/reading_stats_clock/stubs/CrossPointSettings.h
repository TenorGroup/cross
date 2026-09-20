#pragma once

#include <cstdint>

struct FakeSettings {
  uint8_t clockHasBeenSynced = 0;
  uint8_t clockUtcOffsetQ = 48;
};

inline FakeSettings SETTINGS;
