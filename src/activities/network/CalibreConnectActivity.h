#pragma once
#include "activities/X3ActivityLayout.h"

#include <functional>
#include <memory>
#include <string>

#include "activities/Activity.h"
#include "network/CrossPointWebServer.h"

namespace calibre_power {
template <typename Stop, typename Finish>
void stopBeforeFinish(Stop stop, Finish finish) {
  stop();
  finish();
}
}  // namespace calibre_power

enum class CalibreConnectState { WIFI_SELECTION, SERVER_STARTING, SERVER_RUNNING, ERROR };

/**
 * CalibreConnectActivity starts the file transfer server in STA mode,
 * but renders Calibre-specific instructions instead of the web transfer UI.
 */
class CalibreConnectActivity final : public Activity {
  x3activity::InfoPages x3Info;

  CalibreConnectState state = CalibreConnectState::WIFI_SELECTION;

  std::unique_ptr<CrossPointWebServer> webServer;
  std::string connectedIP;
  std::string connectedSSID;
  unsigned long lastHandleClientTime = 0;
  size_t lastProgressReceived = 0;
  size_t lastProgressTotal = 0;
  std::string currentUploadName;
  std::string lastCompleteName;
  unsigned long lastCompleteAt = 0;
  unsigned long lastProcessedCompleteAt = 0;  // Track which server value we've already processed
  bool exitRequested = false;

  void renderServerRunning() const;
  void renderUgly() const;

  void onWifiSelectionComplete(bool connected);
  void startWebServer();
  void stopWebServer();

 public:
  tenorchrome::X3PathIcon navigationIcon() const override { return tenorchrome::X3PathIcon::Network; }
  std::string x3NavigationLabel() const override { return I18N.get(StrId::STR_CALIBRE_WIRELESS); }
  explicit CalibreConnectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("CalibreConnect", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return webServer && webServer->isRunning(); }
  bool preventAutoSleep() override { return webServer && webServer->isRunning(); }
};
