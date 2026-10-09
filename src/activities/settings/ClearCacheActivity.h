#pragma once
#include <I18n.h>

#include <functional>
#include <atomic>

#include "activities/Activity.h"
#include "components/OptionPopup.h"

class ClearCacheActivity final : public Activity {
 public:
  tenorchrome::X3PathIcon navigationIcon() const override { return tenorchrome::X3PathIcon::System; }
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
  int infoPage_ = 0;
  std::atomic<int> infoPages_{1};
  std::string x3InfoText() const;
  void renderX3();
  OptionPopup confirmPopup;
  void beginClear();
  void clearCache();
};
