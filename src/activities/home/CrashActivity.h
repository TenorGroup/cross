#pragma once
#include "activities/X3ActivityLayout.h"
#include "activities/Activity.h"

class CrashActivity final : public Activity {
  x3activity::InfoPages x3Info;

  std::string panicMessage;

 public:
  explicit CrashActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Crash", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
