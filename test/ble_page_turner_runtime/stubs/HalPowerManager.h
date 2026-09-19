#pragma once

class HalPowerManager {
 public:
  class Lock {
   public:
    Lock() = default;
    ~Lock() = default;
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
  };
};
