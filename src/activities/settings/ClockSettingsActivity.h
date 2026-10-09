#pragma once
#include "activities/settings/X3SettingsLayout.h"
#include <string>

#include "activities/UiListActivity.h"

// Clock configuration under System settings: timezone, 12/24-hour format,
// home-header display, manual NTP sync, and Tenor's network auto-timezone
// (clockAutoTimezone). Only reachable when halClock.isAvailable() -
// SettingsActivity gates the entry.
class ClockSettingsActivity final : public UiListActivity {
 public:
  tenorchrome::X3PathIcon navigationIcon() const override { return tenorchrome::X3PathIcon::Device; }
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
  void drawChrome() override {
    if (tenorchrome::x3ScreenLayoutEnabled()) x3settings::drawPath(renderer, headerTitle());
    else UiListActivity::drawChrome();
  }
  const char* headerTitle() const override;
  int settingsChoiceCount(int row) const override;
  SettingsChoiceStyle rowChoiceStyle(int row) const override;
  OptionPopup optionPopup_;
  bool handleCustomInput() override {
    return optionPopup_.handleInput(mappedInput, [this] { requestUpdate(); });
  }
  void render(RenderLock&& lock) override;
  bool supportsFavorites() const override { return true; }
  std::string favoriteKey(int row) const override;

  freeink::ui::ListItem rowItems_[ITEM_COUNT]{};
};
