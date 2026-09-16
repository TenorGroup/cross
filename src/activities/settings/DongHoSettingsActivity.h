#pragma once
#include <I18n.h>

#include <string>

#include "activities/UiListActivity.h"

class DongHoSettingsActivity final : public UiListActivity {
 public:
  static std::string giaTriDong(int index);
  explicit DongHoSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  static constexpr int SO_DONG = 4;

  void onEnter() override;
  std::string navigationLabel() const override { return I18N.get(StrId::STR_CLOCK); }
  void render(RenderLock&&) override;

 private:
  bool supportsFavorites() const override { return true; }
  std::string favoriteKey(int row) const override;
  int listCount() const override { return SO_DONG; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;

  std::string rowValues_[SO_DONG];
  freeink::ui::ListItem rowItems_[SO_DONG]{};
};
