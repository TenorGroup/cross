#pragma once

#include <functional>
#include <string>

#include "MappedInputManager.h"
#include "activities/Activity.h"

class BmpViewerActivity final : public Activity {
 public:
  BmpViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  // Replaces the file list it was opened from: a wake comes back to that folder, the cursor on this image.
  bool wakePlace(WakePlace& place) const override;

 private:
  void loadSiblingImages();
  void doSetSleepCover();
  bool canSetSleepCover() const;
  bool renderImage();
  bool saveJpegSleepCover();

  std::string filePath;
  std::vector<std::string> siblingImages;
  int currentImageIndex = -1;
};
