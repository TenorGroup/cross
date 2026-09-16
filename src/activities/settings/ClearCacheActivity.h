#pragma once
#include <I18n.h>

#include <functional>

#include "activities/Activity.h"
#include "components/OptionPopup.h"

class ClearCacheActivity final : public Activity {
 public:
  explicit ClearCacheActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ClearCache", renderer, mappedInput) {}

  void onEnter() override;
  std::string navigationLabel() const override { return I18N.get(StrId::STR_CLEAR_READING_CACHE); }
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return true; }  // Prevent power-saving mode
  void render(RenderLock&&) override;

 private:
  enum State { WARNING, CLEARING, SUCCESS, FAILED };

  State state = WARNING;

  void goBack() { finish(); }

  int clearedCount = 0;
  int failedCount = 0;
  OptionPopup confirmPopup;
  void beginClear();
  void clearCache();
};
