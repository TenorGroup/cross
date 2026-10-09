#pragma once
#include "activities/X3ActivityLayout.h"
#include "activities/Activity.h"

class CrashActivity final : public Activity {
  x3activity::InfoPages x3Info;

  std::string panicMessage;

 public:
  tenorchrome::X3PathIcon navigationIcon() const override { return tenorchrome::X3PathIcon::System; }
  std::string x3NavigationLabel() const override { return I18N.get(StrId::STR_CRASH_TITLE); }
  explicit CrashActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Crash", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
