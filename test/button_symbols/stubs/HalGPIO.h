#pragma once
// ButtonSymbols.cpp only asks HalGPIO whether the device has a touch panel.
class HalGPIO {
 public:
  bool hasTouch() const { return touch; }
  bool touch = false;
};
