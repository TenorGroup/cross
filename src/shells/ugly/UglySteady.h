#pragma once
// Straight letters for words that must be read right (the keys, "do not cut the power"), at either level of ugliness.
// The pen reads the level from the settings as it draws; while a Steady lives the level is plain. Only under the
// render lock, where nothing else reads or writes the setting.
#include <cstdint>

#include "CrossPointSettings.h"

namespace ugly {

class Steady {
 public:
  Steady() : saved_(SETTINGS.uiUglyLevel) { SETTINGS.uiUglyLevel = 0; }
  ~Steady() { SETTINGS.uiUglyLevel = saved_; }
  Steady(const Steady&) = delete;
  Steady& operator=(const Steady&) = delete;

 private:
  uint8_t saved_;
};

}  // namespace ugly
