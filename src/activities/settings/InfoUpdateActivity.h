#pragma once

#include "SettingsTabs.h"
#include "activities/UiListActivity.h"

// X4 Pro (founder 06/10): About, the panel chip and both updates on one screen of their own, opened from the
// Settings root's "About & updates" row. One rule for where these rows live: the Settings tabs leave them out,
// a pin of one opens this screen.
namespace infoupdate {
// The X4 Pro Settings root: 3 titled groups and this screen (the Tenor touch shell; the ugly shell keeps its own).
bool shown();
// True when `action` lives on this screen and not on a Settings tab. The read-only chip row (no action) asks
// with SettingAction::None and `chip` set.
bool holds(settingstabs::Action action, bool chip = false);
}  // namespace infoupdate

class InfoUpdateActivity final : public UiListActivity {
 public:
  InfoUpdateActivity(GfxRenderer& renderer, MappedInputManager& input)
      : UiListActivity("InfoUpdate", renderer, input, /*wantsTouchLongPress=*/true) {}

 private:
  static constexpr int ROWS = 4;
  freeink::ui::ListItem rows[ROWS]{};

  int listCount() const override { return ROWS; }
  const char* headerTitle() const override { return tr(STR_INFO_UPDATES); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool supportsFavorites() const override { return true; }
  std::string favoriteKey(int row) const override;
};
