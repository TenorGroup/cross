#include "OtaUpdateActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstring>

#include "MappedInputManager.h"
#include "FileTransferState.h"
#include "SdCardFontSystem.h"
#include "SilentRestart.h"
#include "activities/ActivityManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/OtaUpdater.h"

void OtaUpdateActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    LOG_ERR("OTA", "WiFi connection failed, exiting");
#ifdef TENOR_PRESS_PROBE
    if (boot.dryRun())
      logSerial.printf("OTA_DRYRUN_RESULT ok=0 step=wifi err=NO_WIFI run=%d/%d\n",
                       boot.dryRunsTotal - boot.dryRunsLeft + 1, boot.dryRunsTotal);
#endif
    finish();
    return;
  }

  {
    RenderLock lock(*this);
    const auto before = ESP.getFreeHeap();
    sdFontSystem.releaseForOta(renderer);
    LOG_INF("OTA", "Released SD font catalog heap=%u -> %u largest=%u", before, ESP.getFreeHeap(),
            ESP.getMaxAllocHeap());
  }
  LOG_DBG("OTA", "WiFi connected, checking for update");

  {
    RenderLock lock(*this);
    state = CHECKING_FOR_UPDATE;
  }
  requestUpdateAndWait();

  {
    RenderLock lock(*this);
    if (auto* cache = renderer.getFontCacheManager()) cache->releaseSdFontCaches();
  }
#ifdef TENOR_PRESS_PROBE
  if (boot.dryRun()) {
    runDryRun();
    finish();
    return;
  }
#endif
  LOG_INF("OTA", "Manifest start heap=%u largest=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  const auto res = updater.checkForUpdate();
  LOG_INF("OTA", "Manifest check result=%d heap=%u", res, ESP.getFreeHeap());
  // NO_UPDATE here means the release carries no firmware asset for this board
  // (expected until per-board assets are published) - not a failure.
  if (res == OtaUpdater::NO_UPDATE) {
    LOG_DBG("OTA", "No firmware asset for this board in latest release");
    {
      RenderLock lock(*this);
      state = NO_UPDATE;
    }
    return;
  }
  if (res != OtaUpdater::OK) {
    LOG_DBG("OTA", "Update check failed: %d", res);
    {
      RenderLock lock(*this);
      state = FAILED;
    }
    recordAttempt("check");
    return;
  }

  if (!updater.isUpdateNewer()) {
    LOG_DBG("OTA", "No new update available");
    {
      RenderLock lock(*this);
      state = NO_UPDATE;
    }
    return;
  }

#ifdef TENOR_OTA_ACCEPTANCE
  // Dedicated USB-triggered lab build; stable builds always show confirmation.
  runUpdateInstall();
  return;
#endif
  // The user chose Update in the boot before this one: install now, on this boot's heap.
  if (boot.armed) {
    runUpdateInstall();
    return;
  }
  {
    RenderLock lock(*this);
    state = WAITING_CONFIRMATION;
  }
  const char* options[] = {tr(STR_CANCEL), tr(STR_UPDATE)};
  // Default the selection to Update so the hardware Confirm button installs,
  // matching the pre-popup layout (Back = cancel, Confirm = update).
  // Touch: a question over the bar with its one action, nothing marked; "<" or a tap outside cancels.
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  constexpr int skip = 1, preset = -1;
#else
  constexpr int skip = 0, preset = 1;
#endif
  confirmPopup.show(tr(STR_NEW_UPDATE), "", options + skip, 2 - skip, preset, [this](const int idx) {
    // Update: this session's heap is short of Wi-Fi plus a 16 KB TLS record in one piece
    // (measured failing 10 of 10 from Home), so onExit restarts and the next boot installs.
    restartIntoInstall = idx + skip == 1;
    finish();
  });
  requestUpdate();
}

void OtaUpdateActivity::onEnter() {
  runtimeStarted = false;
  idleTimerStarted = false;
  // Claim the radio before Wi-Fi startup or the render wait below. This also
  // covers the auto-connect callback, which can block inside this transition.
  if (!filetransfer::acquire()) {
    LOG_ERR("OTA", "BLE teardown incomplete; leaving OTA update");
    finish();
    return;
  }
  runtimeStarted = true;
  Activity::onEnter();

  // Turn on WiFi immediately
  LOG_DBG("OTA", "Turning on WiFi...");
#ifdef TENOR_PRESS_PROBE
  const uint32_t heapBeforeWifi = ESP.getFreeHeap();
  const uint32_t largestBeforeWifi = ESP.getMaxAllocHeap();
#endif
  WiFi.mode(WIFI_STA);
#ifdef TENOR_PRESS_PROBE
  probeStages = {heapBeforeWifi, largestBeforeWifi, ESP.getFreeHeap(), ESP.getMaxAllocHeap(), 0, 0};
  LOG_INF("OTA", "Wi-Fi start update-boot=%d dry-run=%d/%d heap=%u -> %u largest=%u -> %u", boot.armed,
          boot.dryRun() ? boot.dryRunsTotal - boot.dryRunsLeft + 1 : 0, boot.dryRunsTotal, heapBeforeWifi,
          probeStages.wifiHeap, largestBeforeWifi, probeStages.wifiLargest);
#endif

  // Launch WiFi selection subactivity
  LOG_DBG("OTA", "Launching WifiSelectionActivity...");
  startActivityForResult(makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput, true, false),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void OtaUpdateActivity::onExit() {
  Activity::onExit();

  // Success path reboots via the SHUTTING_DOWN state's plain ESP.restart()
  // (loop() above) so the new firmware boots normally. Back-out paths land
  // here with wifi still active; silent-restart to free the LWIP/mbedTLS
  // fragmentation, same as the other wifi activities. Update restarts into
  // the update boot instead, and the update boot itself always restarts: it
  // never runs the normal screens, which it started without their fonts.
  const bool wifiOn = runtimeStarted && WiFi.getMode() != WIFI_MODE_NULL;
  if (wifiOn) {
    WiFi.disconnect(false);
    delay(30);
  }
  if (restartIntoInstall) {
    silentRestartToOta();
#ifdef TENOR_PRESS_PROBE
  } else if (dryRunsAfter > 0) {
    silentRestartToOta(dryRunsAfter, boot.dryRunsTotal);
#endif
  } else if (wifiOn || boot.armed) {
    silentRestart();
  }
  // Release after Wi-Fi teardown. The main loop may resume BLE once this owner
  // and any nested owner have both gone away.
  filetransfer::release();
}

void OtaUpdateActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // During the download a frame runs beside the TLS reads on the same heap: decide first, and a
  // skipped percent touches nothing (it used to clear and redraw the header, then return).
  float updaterProgress = 0;
  const bool firstProgressFrame = state == UPDATE_IN_PROGRESS && lastUpdaterPercentage == UNINITIALIZED_PERCENTAGE;
  if (state == UPDATE_IN_PROGRESS) {
    LOG_DBG("OTA", "Update progress: %d / %d", updater.getProcessedSize(), updater.getTotalSize());
    updaterProgress = static_cast<float>(updater.getProcessedSize()) / static_cast<float>(updater.getTotalSize());
    // Only update every 2% at the most
    if (static_cast<int>(updaterProgress * 50) == lastUpdaterPercentage / 2) {
      return;
    }
    lastUpdaterPercentage = static_cast<int>(updaterProgress * 100);
  }
#ifdef TENOR_PRESS_PROBE
  const uint32_t frameHeap = ESP.getFreeHeap();
  const uint32_t frameLargest = ESP.getMaxAllocHeap();
#endif

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_UPDATE));
  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - height) / 2;

  if (state == CHECKING_FOR_UPDATE) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_CHECKING_UPDATE));
  } else if (state == WAITING_CONFIRMATION) {
    // Version info sits in the upper part of the screen so the centered
    // Cancel/Update popup doesn't cover it (same layout as ConfirmationActivity).
    const int infoTop = pageHeight / 6;
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, infoTop,
                      (std::string(tr(STR_CURRENT_VERSION)) + CROSSPOINT_VERSION).c_str());
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, infoTop + height + metrics.verticalSpacing,
                      (std::string(tr(STR_NEW_VERSION)) + updater.getLatestVersion()).c_str());

    if (confirmPopup.processRender(renderer, mappedInput)) return;
  } else if (state == UPDATE_IN_PROGRESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATING));

    int y = top + height + metrics.verticalSpacing;
    GUI.drawProgressBar(
        renderer,
        Rect{metrics.contentSidePadding, y, pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
        static_cast<int>(updaterProgress * 100), 100);

    y += metrics.progressBarHeight + metrics.verticalSpacing;
    // Percent label is drawn by BaseTheme::drawProgressBar; this slot is left intentionally empty
    // so the bytes line below stays at the same Y it was at when the activity drew its own percent.
    y += height + metrics.verticalSpacing;
    // The first frame comes before any connection opens: every digit a later frame draws is
    // decoded now, in white on the white page, so a compressed UI font sizes its glyph buffers
    // here and later frames reuse them.
    if (firstProgressFrame) renderer.drawCenteredText(UI_10_FONT_ID, y, "0123456789 /%", false);
    char bytes[32];
    snprintf(bytes, sizeof(bytes), "%u / %u", static_cast<unsigned>(updater.getProcessedSize()),
             static_cast<unsigned>(updater.getTotalSize()));
    renderer.drawCenteredText(UI_10_FONT_ID, y, bytes);
  } else if (state == NO_UPDATE) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_NO_UPDATE), true, EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_FAILED), true, EpdFontFamily::BOLD);
    if (failedDetail != nullptr) {
      renderer.drawCenteredText(UI_10_FONT_ID, top + height + metrics.verticalSpacing, failedDetail);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == FINISHED) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_COMPLETE), true, EpdFontFamily::BOLD);
    // Wrapped (#3632): the Vietnamese hint text is longer than English and was
    // clipping at one line.
    const int hintY = top + height + metrics.verticalSpacing;
    const Rect hintBounds{metrics.contentSidePadding, hintY, pageWidth - metrics.contentSidePadding * 2,
                          pageHeight - hintY};
    UITheme::drawCenteredWrappedText(renderer, hintBounds, UI_10_FONT_ID, tr(STR_POWER_ON_HINT), 3, true,
                                     EpdFontFamily::REGULAR, UITheme::TextVerticalAlignment::TOP);
  }

  renderer.displayBuffer();
#ifdef TENOR_PRESS_PROBE
  if (state == UPDATE_IN_PROGRESS)
    LOG_INF("OTA", "Frame %u%% heap=%u -> %u largest=%u -> %u", lastUpdaterPercentage, frameHeap, ESP.getFreeHeap(),
            frameLargest, ESP.getMaxAllocHeap());
#endif
}

void OtaUpdateActivity::recordAttempt(const char* op, const bool now) {
  ota_log::stage(op, updater.lastAttempt());
  if (now)
    ota_log::writeStaged();
  else
    activityManager.deferWrite(ota_log::writeStaged);
}

#ifdef TENOR_PRESS_PROBE
void probeKeepDryRunLine(const char* line);  // main.cpp

// The real check and download against a test manifest, verified and closed like an install,
// with the boot slot untouched. One run a boot; one machine-readable line, free heap around it.
void OtaUpdateActivity::runDryRun() {
  updater.setDryRun(dryRunUrl.c_str());
  const int run = boot.dryRunsTotal - boot.dryRunsLeft + 1;
  const uint32_t before = ESP.getFreeHeap();
  const uint32_t beforeLargest = ESP.getMaxAllocHeap();
  LOG_INF("OTA", "Manifest start heap=%u largest=%u", before, beforeLargest);
  const auto checked = updater.checkForUpdate();
  const uint32_t checkMs = updater.lastAttempt().ms;
  if (checked == OtaUpdater::OK) runUpdateInstall();
  // A progress frame may still be painting, and the card shares its bus: wait for it.
  RenderLock lock(*this);
  const uint32_t after = ESP.getFreeHeap();
  const uint32_t afterLargest = ESP.getMaxAllocHeap();
  // What the progress frames' title glyphs still hold, apart from what the transfer left.
  if (auto* cache = renderer.getFontCacheManager()) cache->releaseBuiltinPageCaches();
  char fields[ota_log::LINE_BYTES];
  ota_log::formatFields(fields, sizeof(fields), updater.lastAttempt());
  char line[PROBE_LINE_BYTES];
  snprintf(line, sizeof(line),
           "OTA_DRYRUN_RESULT %s op=%s run=%d/%d check_ms=%u parts=%u retries=%u boot=%u/%u wifi=%u/%u install=%u/%u "
           "before=%u before_largest=%u after=%u after_largest=%u after_fonts=%u after_fonts_largest=%u\n",
           fields, checked == OtaUpdater::OK ? "install" : "check", run, boot.dryRunsTotal,
           static_cast<unsigned>(checkMs), static_cast<unsigned>(updater.lastParts()),
           static_cast<unsigned>(updater.lastRetries()), static_cast<unsigned>(probeStages.bootHeap),
           static_cast<unsigned>(probeStages.bootLargest), static_cast<unsigned>(probeStages.wifiHeap),
           static_cast<unsigned>(probeStages.wifiLargest), static_cast<unsigned>(probeStages.installHeap),
           static_cast<unsigned>(probeStages.installLargest), static_cast<unsigned>(before),
           static_cast<unsigned>(beforeLargest), static_cast<unsigned>(after), static_cast<unsigned>(afterLargest),
           static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
  logSerial.print(line);
  // The restart that follows drops the port: the next boot prints this line again (main.cpp).
  probeKeepDryRunLine(line);
  recordAttempt("dryrun", true);
  // Back ends the series; any other result goes on to the next run, in a boot of its own.
  if (std::strcmp(updater.lastAttempt().err, "CANCELLED_ERROR") != 0) dryRunsAfter = boot.dryRunsLeft - 1;
}
#endif

void OtaUpdateActivity::runUpdateInstall() {
  LOG_DBG("OTA", "New update available, starting download...");
  {
    RenderLock lock(*this);
    state = UPDATE_IN_PROGRESS;
    // Font caches go before the first progress frame, which then sizes the few glyph buffers the
    // later frames reuse: nothing is allocated for a frame while the download runs.
    if (auto* cache = renderer.getFontCacheManager()) cache->releaseSdFontCaches();
  }
  requestUpdateAndWait();
  LOG_INF("OTA", "Install start heap=%u largest=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#ifdef TENOR_PRESS_PROBE
  probeStages.installHeap = ESP.getFreeHeap();
  probeStages.installLargest = ESP.getMaxAllocHeap();
#endif
  if (!backLatch.start(gpio, mappedInput.physicalBack())) LOG_ERR("OTA", "Cannot start Back sampler");
  updater.setCancelCheck([](void* ctx) { return static_cast<OtaUpdateActivity*>(ctx)->backLatch.latched(); }, this);
  const auto res = updater.installUpdate(
      [](void* ctx) {
        // immediate=true notifies the render task directly. The default deferred path only
        // sets a flag consumed at the end of ActivityManager::loop(), which never runs while
        // installUpdate() blocks this task.
        static_cast<OtaUpdateActivity*>(ctx)->requestUpdate(true);
      },
      this);
  backLatch.stop();

  LOG_INF("OTA", "Install result=%d bytes=%u", res, static_cast<unsigned>(updater.getProcessedSize()));
  // Dry run (probe): the caller prints and records each run; no result screen.
  if (updater.isDryRun()) return;
  // Back leaves at once: its own press and release are still queued for the next loop pass,
  // so a result screen would close on them anyway. The old firmware keeps running.
  if (res == OtaUpdater::CANCELLED_ERROR) {
    // The Back release queued while the install blocked this loop would otherwise close the
    // parent screen as well.
    mappedInput.suppressNextRelease(MappedInputManager::Button::Back);
    finish();
    recordAttempt("install");
    return;
  }
  if (res != OtaUpdater::OK) {
    LOG_DBG("OTA", "Update failed: %d", res);
    {
      RenderLock lock(*this);
      failedDetail = res == OtaUpdater::WRONG_DEVICE_ERROR ? tr(STR_FIRMWARE_WRONG_DEVICE) : nullptr;
      state = FAILED;
    }
    requestUpdate();
    recordAttempt("install");
    return;
  }

  {
    RenderLock lock(*this);
    state = FINISHED;
  }
  requestUpdateAndWait();
  recordAttempt("install", true);
  // Hold the completion screen briefly so the user sees it, then restart.
  delay(3000);
  {
    RenderLock lock(*this);
    state = SHUTTING_DOWN;
  }
}

bool OtaUpdateActivity::idleExitDue(const unsigned long now, const bool interaction) {
  const bool waiting = state == WAITING_CONFIRMATION || state == FAILED || state == NO_UPDATE;
  const bool radioAlive = runtimeStarted && WiFi.getMode() != WIFI_MODE_NULL;
  return ota_power::idleExitDue(static_cast<uint32_t>(now), waiting, radioAlive, interaction, idleSince,
                                idleTimerStarted);
}

void OtaUpdateActivity::loop() {
  const bool interaction = mappedInput.wasAnyPressed() || mappedInput.wasAnyReleased() ||
                           mappedInput.wasScreenTouchReleased();
  if (idleExitDue(millis(), interaction)) {
    finish();
    return;
  }

  if (state == WAITING_CONFIRMATION) {
    if (confirmPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;
    // Popup dismissed without a selection (Back button or tap outside): cancel.
    finish();
    return;
  }

  if (state == FAILED) {
    int x = 0;
    int y = 0;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
      finish();
    }
    return;
  }

  if (state == NO_UPDATE) {
    int x = 0;
    int y = 0;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
      finish();
    }
    return;
  }

  if (state == SHUTTING_DOWN) {
    ESP.restart();
  }
}
