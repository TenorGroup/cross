#pragma once
#include <string>

#include "activities/UiListActivity.h"

// Clock configuration under System settings: timezone, 12/24-hour format,
// home-header display, manual NTP sync, and Tenor's network auto-timezone
// (clockAutoTimezone). Only reachable when halClock.isAvailable() -
// SettingsActivity gates the entry.
class ClockSettingsActivity final : public UiListActivity {
 public:
  explicit ClockSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  static constexpr int ITEM_COUNT = 6;

  // Row value text, for the favourites preview (MenuFavorites) as well as
  // this screen's own buildScreen().
  static std::string giaTriDong(int index);

  void onEnter() override;

 private:
  int listCount() const override { return ITEM_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  bool supportsFavorites() const override { return true; }
  std::string favoriteKey(int row) const override;

  freeink::ui::ListItem rowItems_[ITEM_COUNT]{};
};
