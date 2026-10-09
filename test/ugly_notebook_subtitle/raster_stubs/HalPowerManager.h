#pragma once
class HalPowerManager {
 public:
  int getDisplayedBatteryPercentage() const { return 80; }
};
extern HalPowerManager powerManager;
