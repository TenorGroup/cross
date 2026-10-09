#pragma once
#include "activities/settings/X3SettingsLayout.h"
#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// Timezone selection list (src/util/Timezones.cpp table). Selecting an entry
// persists it, re-applies the clock's TZ rule, and returns.
class TimezonePickerActivity final : public UiListActivity {
 public:
  tenorchrome::X3PathIcon navigationIcon() const override { return tenorchrome::X3PathIcon::Device; }
  explicit TimezonePickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 private:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void drawChrome() override {
    if (tenorchrome::x3ScreenLayoutEnabled()) x3settings::drawPath(renderer, headerTitle());
    else UiListActivity::drawChrome();
  }
  const char* headerTitle() const override;

  std::vector<std::string> offsetLabels_;
  std::vector<freeink::ui::ListItem> rowItems_;
};
