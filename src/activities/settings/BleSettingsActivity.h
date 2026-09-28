#pragma once

#include <GfxRenderer.h>

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

class MappedInputManager;

// Bluetooth page turner: switch it on, scan, pair a remote, forget one. The radio and the remote
// belong to lib/BlePageTurner; this screen only asks it.
class BleSettingsActivity final : public UiListActivity {
 public:
  explicit BleSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("BleSettings", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;

 private:
  int listCount() const override { return static_cast<int>(rows.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  uint32_t signature() const;
  void refresh();

  // Written on the main loop under the render lock, read by buildScreen on the render task.
  std::vector<freeink::ui::ListItem> rows;
  std::vector<std::string> labels;
  std::string status;
  uint32_t shownSignature = 0;
  unsigned long lastPollMs = 0;
};
