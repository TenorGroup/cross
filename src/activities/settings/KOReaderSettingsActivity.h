#pragma once
#include "activities/settings/X3SettingsLayout.h"

#include <string>

#include "activities/UiListActivity.h"

/**
 * Submenu for KOReader Sync settings.
 * Shows username, password, and authenticate options.
 */
class KOReaderSettingsActivity final : public UiListActivity {
 public:
  tenorchrome::X3PathIcon navigationIcon() const override { return tenorchrome::X3PathIcon::Network; }
  explicit KOReaderSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  static constexpr int MENU_ITEMS = 8;

 private:
  bool supportsFavorites() const override { return true; }
  std::string favoriteKey(int row) const override;
  int listCount() const override;
  int settingsChoiceCount(int row) const override { return row >= 3 && row <= 5 ? 2 : 0; }
  SettingsChoiceStyle rowChoiceStyle(int row) const override {
    return settingsChoiceStyle(settingsChoiceCount(row), tenorchrome::kTouchShell, row == 4);
  }
  OptionPopup optionPopup_;
  OptionPopup* tiltPopup() override { return &optionPopup_; }
  bool handleCustomInput() override {
    return optionPopup_.handleInput(mappedInput, [this] { requestUpdate(); });
  }
  void render(RenderLock&& lock) override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void drawChrome() override {
    if (tenorchrome::x3ScreenLayoutEnabled()) x3settings::drawPath(renderer, headerTitle());
    else UiListActivity::drawChrome();
  }
  const char* headerTitle() const override;

  // Row storage: MENU_ITEMS is a compile-time constant, so fixed-capacity
  // storage avoids any heap allocation for the row list. Labels are set once
  // in the constructor; buildScreen() only refreshes the live value text
  // (rowValues_) by assigning into the existing strings (no array growth).
  std::string rowValues_[MENU_ITEMS];
  freeink::ui::ListItem rowItems_[MENU_ITEMS]{};
};
