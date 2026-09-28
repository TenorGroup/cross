#pragma once
#include <string>

#include "activities/UiListActivity.h"

// Read-only device information: detected hardware (device profile, display
// controller, touch, frontlight, RTC, IMU), firmware version, and MAC address.
// Rows are informational; activating one does nothing.
class AboutActivity final : public UiListActivity {
 public:
  explicit AboutActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  static constexpr int ITEM_COUNT = 11;
  // Raw BQ27220 registers, appended after ITEM_COUNT only on a board with the gauge (X3):
  // voltage, average current, remaining/full/design capacity, state of charge, state of
  // health, cycle count, BatteryStatus flags. See HalPowerManager::GaugeDiagnostics.
  static constexpr int GAUGE_ITEM_COUNT = 9;
  static constexpr int MAX_ITEM_COUNT = ITEM_COUNT + GAUGE_ITEM_COUNT;

  void onEnter() override;

 private:
  int listCount() const override { return hasGauge_ ? MAX_ITEM_COUNT : ITEM_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int) override {}
  const char* headerTitle() const override;
  // Refills the gauge rows from HalPowerManager's cache; called from buildScreen() on every
  // render (the first one included) so a screen left open keeps showing the latest
  // 30s-throttled read.
  void refreshGaugeRows();

  bool hasGauge_ = false;
  std::string rowValues_[MAX_ITEM_COUNT];
  freeink::ui::ListItem rowItems_[MAX_ITEM_COUNT]{};
};
