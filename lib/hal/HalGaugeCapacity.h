#pragma once

#include "Bq27220Capacity.h"

class HalGaugeCapacity;
extern HalGaugeCapacity halGaugeCapacity;  // Singleton

// Loads the board's battery capacity (BoardConfig batteryGauge.designCapacityMah) into its
// BQ27220 through Bq27220Capacity. The gauge is reached only from the task
// HalPowerManager::mayReadGauge() allows, and the load never blocks that task.
class HalGaugeCapacity {
  Bq27220Capacity load;
  bool started = false;

 public:
  // The main loop, once the first frame is up: makes what is due of the load, then returns.
  void tick();
  // Before deep sleep: a load under way leaves CONFIG UPDATE and seals the gauge at once.
  void abandon();
  // "none", "pending", "not-needed", "loaded", "resealed" or "failed:<stage>", for the probe build.
  const char* status() const;
};
