#pragma once

#include "activities/UiListActivity.h"

class SettingsChoiceActivity final : public UiListActivity {
 public:
  SettingsChoiceActivity(GfxRenderer& renderer, MappedInputManager& input, std::string title,
                         std::vector<std::string> labels, int selected, std::function<void(int)> onSelect);
  bool remembersNavigation() const override { return false; }
  void onEnter() override;

 private:
  std::string title_;
  std::vector<std::string> labels_;
  std::vector<freeink::ui::ListItem> rows_;
  int selected_;
  std::function<void(int)> onSelect_;
  int listCount() const override { return static_cast<int>(labels_.size()); }
  const char* headerTitle() const override { return title_.c_str(); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
};
