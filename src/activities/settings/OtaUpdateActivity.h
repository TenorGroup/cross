#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "network/UpdateBoot.h"
#include "network/OtaUpdater.h"
#include "util/FileTransferBackLatch.h"

namespace ota_power {
constexpr uint32_t IDLE_TIMEOUT_MS = 5u * 60u * 1000u;

inline bool idleExitDue(const uint32_t now, const bool waiting, const bool radioAlive, const bool interaction,
                        uint32_t& idleSince, bool& timerStarted) {
  if (!waiting || !radioAlive) {
    timerStarted = false;
    return false;
  }
  if (!timerStarted || interaction) {
    idleSince = now;
    timerStarted = true;
    return false;
  }
  return now - idleSince >= IDLE_TIMEOUT_MS;
}
}  // namespace ota_power

class OtaUpdateActivity : public Activity {
  enum State {
    WIFI_SELECTION,
    CHECKING_FOR_UPDATE,
    WAITING_CONFIRMATION,
    UPDATE_IN_PROGRESS,
    NO_UPDATE,
    FAILED,
    FINISHED,
    SHUTTING_DOWN
  };

  // Can't initialize this to 0 or the first render doesn't happen
  static constexpr unsigned int UNINITIALIZED_PERCENTAGE = 111;

  State state = WIFI_SELECTION;
  unsigned int lastUpdaterPercentage = UNINITIALIZED_PERCENTAGE;
  bool runtimeStarted = false;
  uint32_t idleSince = 0;
  bool idleTimerStarted = false;
  OtaUpdater updater;
  // Optional detail line shown under the generic "Update failed" heading.
  // Points into the i18n string table (flash-resident, so no lifetime concern);
  // nullptr means no extra detail.
  const char* failedDetail = nullptr;
  // Cancel/Update confirmation over the version info (replaces the old
  // hand-rolled bottom tap rects).
  OptionPopup confirmPopup;
  // The download holds the loop; a Back tap is sampled on its own task meanwhile.
  FileTransferBackLatch backLatch;
  // Set when this screen was restarted into (the user already chose Update, or a dry run): it
  // installs without asking again and restarts on its way out, whatever happened.
  const update_boot::Request boot;
  // The user chose Update: onExit restarts into the update boot instead of Home.
  bool restartIntoInstall = false;

  void onWifiSelectionComplete(bool success);
  void runUpdateInstall();
  bool renderUglyNote(int percent, bool firstProgressFrame) const;
  // One line in the card's OTA log, after the result screen is drawn (now: it already is).
  void recordAttempt(const char* op, bool now = false);
  bool idleExitDue(unsigned long now, bool interaction);
#ifdef TENOR_PRESS_PROBE
  static constexpr size_t PROBE_LINE_BYTES = 640;
  // Free heap and largest block when this screen came up, after Wi-Fi started, at install start.
  struct {
    uint32_t bootHeap, bootLargest, wifiHeap, wifiLargest, installHeap, installLargest;
  } probeStages = {};
  std::string dryRunUrl;
  // Dry runs still to do after this boot's; the next boot does the next one.
  uint8_t dryRunsAfter = 0;
  void runDryRun();
#endif

 public:
  explicit OtaUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                             const update_boot::Request& bootRequest = {})
      : Activity("OtaUpdate", renderer, mappedInput), updater(), boot(bootRequest) {}
#ifdef TENOR_PRESS_PROBE
  // CMD:OTA_DRYRUN, in the update boot: after Wi-Fi joins, check and download from manifestUrl once.
  void setDryRun(const char* manifestUrl) { dryRunUrl = manifestUrl; }
#endif
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == CHECKING_FOR_UPDATE || state == UPDATE_IN_PROGRESS; }
  bool skipLoopDelay() override { return true; }  // Prevent power-saving mode
};
