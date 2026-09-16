#pragma once
#include "activities/Activity.h"
class ChapterNumberEntryActivity final : public Activity {
 public:
  ChapterNumberEntryActivity(GfxRenderer& renderer, MappedInputManager& input, uint32_t value);
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  char digits[7] = "000001";
  int cursor = 5;
};
