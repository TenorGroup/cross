#pragma once
#include "activities/settings/X3SettingsLayout.h"

#include <GfxRenderer.h>

#include "activities/UiListActivity.h"
#include "activities/util/KeyboardLayoutSet.h"

class MappedInputManager;

class KeyboardLayoutsActivity final : public UiListActivity {
 public:
  tenorchrome::X3PathIcon navigationIcon() const override { return tenorchrome::X3PathIcon::Keyboard; }
  explicit KeyboardLayoutsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("KeyboardLayouts", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;

 private:
  int listCount() const override { return keyboard_layouts::COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void drawChrome() override {
    if (tenorchrome::x3ScreenLayoutEnabled()) x3settings::drawPath(renderer, headerTitle(), icon_tenor_settings_ban_phim_40);
    else UiListActivity::drawChrome();
  }
  const char* headerTitle() const override;

  bool isLocked(uint8_t i) const;

  freeink::ui::ListItem rowItems[keyboard_layouts::COUNT]{};

  uint16_t workingMask = 0;
  // Do not persist a derived default merely because the screen was visited.
  bool edited = false;
};
