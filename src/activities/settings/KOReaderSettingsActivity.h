#pragma once
#include "activities/settings/X3SettingsLayout.h"

#include <string>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

/**
 * Submenu for KOReader Sync settings.
 * Shows username, password, and authenticate options.
 */
class KOReaderSettingsActivity final : public UiListActivity {
 public:
  tenorchrome::X3PathIcon navigationIcon() const override { return tenorchrome::X3PathIcon::Network; }
  explicit KOReaderSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  // Upstream's twelve rows plus automatic sync.
  static constexpr int MENU_ITEMS = 13;

  // Row order.
  static constexpr int ROW_USERNAME = 0;
  static constexpr int ROW_PASSWORD = 1;
  static constexpr int ROW_SERVER_URL = 2;
  static constexpr int ROW_SERVER_TYPE = 3;
  static constexpr int ROW_CUSTOM_HEADER_1 = 4;
  static constexpr int ROW_CUSTOM_HEADER_2 = 5;
  static constexpr int ROW_MATCH_METHOD = 6;
  static constexpr int ROW_SEND_METADATA = 7;
  static constexpr int ROW_SYNC_BEHAVIOR = 8;
  static constexpr int ROW_AUTO_SYNC = 9;
  static constexpr int ROW_SYNC_CLIPPINGS = 10;
  static constexpr int ROW_SIGN_UP = 11;
  static constexpr int ROW_AUTHENTICATE = 12;

 private:
  bool supportsFavorites() const override { return true; }
  std::string favoriteKey(int row) const override;
  int listCount() const override;
  // Server type is a three-way popup; matching and sync behavior are two-way popups; send metadata,
  // automatic sync and sync clippings are plain On/Off switches.
  int settingsChoiceCount(int row) const override {
    if (row == ROW_SERVER_TYPE) return 3;
    return row >= ROW_MATCH_METHOD && row <= ROW_SYNC_CLIPPINGS ? 2 : 0;
  }
  SettingsChoiceStyle rowChoiceStyle(int row) const override {
    return settingsChoiceStyle(settingsChoiceCount(row), tenorchrome::kTouchShell,
                               row == ROW_SEND_METADATA || row == ROW_AUTO_SYNC || row == ROW_SYNC_CLIPPINGS);
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
