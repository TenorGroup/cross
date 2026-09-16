#pragma once
#include <array>

#include "activities/UiListActivity.h"
#include "util/ReadingHabits.h"
class ReadingHabitsActivity final : public UiListActivity {
 public:
  ReadingHabitsActivity(GfxRenderer& r, MappedInputManager& input);
  void onEnter() override;

 protected:
  int listCount() const override { return habits::NAMES; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void drawFooter() override;
  void drawChrome() override;
  void activateIndex(int index) override;

 private:
  std::array<freeink::ui::ListItem, habits::NAMES> rows{};
  habits::Summary summary{};
  uint8_t awarded = 0, hidden = 0;
  bool readable = true, failed = false;
  void refreshRows();
};
