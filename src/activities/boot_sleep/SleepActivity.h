#pragma once
#include <string>

#include "activities/Activity.h"

class Bitmap;
class HalFile;

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool fromTimeout = false)
      : Activity("Sleep", renderer, mappedInput), fromTimeout(fromTimeout) {}
  void onEnter() override;
  static void showEnteringSleep(GfxRenderer& renderer);
  // The same notice in the same place when waking, over the sleep screen the panel still shows.
  static void showStartingUp(GfxRenderer& renderer);
  // True once, after a wake notice started its refresh: the first screen then waits it out.
  static bool takeWakeNoticeRunning();
#ifdef TENOR_WAKE_LABEL_GRAY
  // The same, over a sleep screen with no kept frame (a gray one); off unless the build asks for it.
  static void showStartingUpOverGray(GfxRenderer& renderer);
#endif

 private:
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  void renderCoverSleepScreen() const;
  void renderBitmapSleepScreen(const Bitmap& bitmap, bool preserveBackground = false) const;
  bool renderSleepOverlayFile(HalFile& file, const char* pathForLog) const;
  bool renderTransparentOverlayPng(const std::string& path) const;
  bool renderSleepOverlayPath(const std::string& path) const;
  void renderLastScreenSleepScreen() const;
  void renderTransparentCustomSleepScreen() const;
  void renderBlankSleepScreen() const;
  // Man ngu mac dinh cua tenor/cross, an pham nen thang vao firmware.
  void renderTenorSleepScreen() const;
  void renderStatsSleepScreen() const;
  // A random saved quote with its book's small cover, title and place (mockup S2).
  void renderQuoteSleepScreen() const;

  bool fromTimeout = false;
};
