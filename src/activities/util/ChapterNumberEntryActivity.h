#pragma once
#include "activities/X3ActivityLayout.h"
#include "activities/Activity.h"
class ChapterNumberEntryActivity final : public Activity {
  x3activity::InfoPages x3Info;

 public:
  tenorchrome::X3PathIcon navigationIcon() const override { return tenorchrome::X3PathIcon::Position; }
  std::string x3NavigationLabel() const override { return I18N.get(StrId::STR_GO_TO_CHAPTER_NUMBER); }
  ChapterNumberEntryActivity(GfxRenderer& renderer, MappedInputManager& input, uint32_t value);
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  char digits[7] = "000001";
  int cursor = 5;
};
