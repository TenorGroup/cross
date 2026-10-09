#pragma once
#include "activities/settings/X3SettingsLayout.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "activities/UiListActivity.h"

class MappedInputManager;

/**
 * Activity for selecting UI language
 */
class LanguageSelectActivity final : public UiListActivity {
 public:
  tenorchrome::X3PathIcon navigationIcon() const override { return tenorchrome::X3PathIcon::Language; }
  explicit LanguageSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 private:
  int listCount() const override { return totalItems; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void drawChrome() override {
    if (tenorchrome::x3ScreenLayoutEnabled()) x3settings::drawPath(renderer, headerTitle(), icon_tenor_settings_ngon_ngu_40);
    else UiListActivity::drawChrome();
  }
  const char* headerTitle() const override;

  // Only the languages compiled into this build (custom_i18n_builtin_langs), not the whole enum.
  constexpr static uint8_t totalItems = sizeof(SORTED_LANGUAGE_INDICES) / sizeof(SORTED_LANGUAGE_INDICES[0]);

  // Row storage: totalItems is a compile-time constant, so a fixed-capacity
  // array avoids any heap allocation for the row list. Built once in
  // onEnter() — activateIndex() finishes the activity immediately on
  // selection, so buildScreen() never needs to see a different "Selected"
  // row within one visit.
  freeink::ui::ListItem rowItems[totalItems]{};
};
