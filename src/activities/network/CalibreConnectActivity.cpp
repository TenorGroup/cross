#include "CalibreConnectActivity.h"

#include <ESPmDNS.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "CrossPointSettings.h"
#include "UIFontTiers.h"
#include "WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "shells/Shell.h"
#include "shells/ugly/UglyNote.h"
#include "util/TaskWatchdog.h"

namespace {
constexpr const char* HOSTNAME = "crosspoint";
}  // namespace

void CalibreConnectActivity::onEnter() {
  Activity::onEnter();

  requestUpdate();
  state = CalibreConnectState::WIFI_SELECTION;
  connectedIP.clear();
  connectedSSID.clear();
  lastHandleClientTime = 0;
  lastProgressReceived = 0;
  lastProgressTotal = 0;
  currentUploadName.clear();
  lastCompleteName.clear();
  lastCompleteAt = 0;
  lastProcessedCompleteAt = 0;
  exitRequested = false;

  if (WiFi.status() != WL_CONNECTED) {
    startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, true, false),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& wifi = std::get<WifiResult>(result.data);
                               connectedIP = wifi.ip;
                               connectedSSID = wifi.ssid;
                             }
                             onWifiSelectionComplete(!result.isCancelled);
                           });
  } else {
    connectedIP = WiFi.localIP().toString().c_str();
    connectedSSID = WiFi.SSID().c_str();
    startWebServer();
  }
}

void CalibreConnectActivity::onExit() {
  Activity::onExit();

  stopWebServer();
  MDNS.end();

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void CalibreConnectActivity::onWifiSelectionComplete(const bool connected) {
  if (!connected) {
    finish();
    return;
  }

  startWebServer();
}

void CalibreConnectActivity::startWebServer() {
  state = CalibreConnectState::SERVER_STARTING;
  requestUpdate();

  MDNS.end();
  if (MDNS.begin(HOSTNAME)) {
    // mDNS is optional for the Calibre plugin but still helpful for users.
    LOG_DBG("CAL", "mDNS started: http://%s.local/", HOSTNAME);
  }

  // Heap-critical allocation: SD-font caches retained for the CJK UI fallback
  // are rebuildable - release them (again: the WiFi selection screen may have
  // repopulated them rendering a CJK SSID) so the server object doesn't abort
  // on OOM. See CrossPointWebServerActivity::startWebServer().
  if (auto* fcm = renderer.getFontCacheManager()) {
    LOG_DBG("CAL", "Free heap before SD font cache release: %d bytes", ESP.getFreeHeap());
    {
      RenderLock lock(*this);
      fcm->releaseSdFontCaches();
    }
    LOG_DBG("CAL", "Free heap before server alloc: %d bytes", ESP.getFreeHeap());
  }

  webServer.reset(new CrossPointWebServer());
  webServer->setUiTextSizeApplier([this](const uint8_t size) {
    RenderLock lock(*this);
    if (!applyUiFontSize(renderer, size)) return false;
    SETTINGS.uiTextSize = size;
    UITheme::getInstance().reload();
    requestUpdate();
    return true;
  });
  webServer->begin();

  if (webServer->isRunning()) {
    state = CalibreConnectState::SERVER_RUNNING;
    requestUpdate();
  } else {
    state = CalibreConnectState::ERROR;
    requestUpdate();
  }
}

void CalibreConnectActivity::stopWebServer() {
  if (webServer) {
    webServer->stop();
    webServer.reset();
  }
}

void CalibreConnectActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    exitRequested = true;
  }

  if (webServer && webServer->isRunning()) {
    const unsigned long timeSinceLastHandleClient = millis() - lastHandleClientTime;
    if (lastHandleClientTime > 0 && timeSinceLastHandleClient > 100) {
      LOG_DBG("CAL", "WARNING: %lu ms gap since last handleClient", timeSinceLastHandleClient);
    }

    resetTaskWatchdogIfSubscribed();
    constexpr int MAX_ITERATIONS = 80;
    for (int i = 0; i < MAX_ITERATIONS && webServer->isRunning(); i++) {
      webServer->handleClient();
      if (exitRequested || webServer->sessionIdleExpired(millis())) {
        exitRequested = true;
        break;
      }
      if ((i & 0x07) == 0x07) {
        resetTaskWatchdogIfSubscribed();
      }
      if ((i & 0x0F) == 0x0F) {
        yield();
        if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
          exitRequested = true;
          break;
        }
      }
    }
    lastHandleClientTime = millis();

    const auto status = webServer->getWsUploadStatus();
    bool changed = false;
    if (status.inProgress) {
      if (status.received != lastProgressReceived || status.total != lastProgressTotal ||
          status.filename != currentUploadName) {
        lastProgressReceived = status.received;
        lastProgressTotal = status.total;
        currentUploadName = status.filename;
        changed = true;
      }
    } else if (lastProgressReceived != 0 || lastProgressTotal != 0) {
      lastProgressReceived = 0;
      lastProgressTotal = 0;
      currentUploadName.clear();
      changed = true;
    }
    // Only update lastCompleteAt if the server has a NEW value (not one we already processed)
    // This prevents restoring an old value after the 6s timeout clears it
    if (status.lastCompleteAt != 0 && status.lastCompleteAt != lastProcessedCompleteAt) {
      lastCompleteAt = status.lastCompleteAt;
      lastCompleteName = status.lastCompleteName;
      lastProcessedCompleteAt = status.lastCompleteAt;  // Mark this value as processed
      changed = true;
    }
    if (lastCompleteAt > 0 && (millis() - lastCompleteAt) >= 6000) {
      lastCompleteAt = 0;
      lastCompleteName.clear();
      // Note: we DON'T reset lastProcessedCompleteAt here, so we won't re-process the old server value
      changed = true;
    }
    if (changed) {
      requestUpdate();
    }
  }

  if (exitRequested) {
    calibre_power::stopBeforeFinish([this] { stopWebServer(); }, [this] { finish(); });
    return;
  }
}

// The same page by hand: the steps written in pen, the network and the address in the UI font, a received file
// with a pen-hatched bar. Starting and failing are note pages.
void CalibreConnectActivity::renderUgly() const {
  [[maybe_unused]] const uint32_t started = millis();
  ugly::Hints hints;
  hints.back = true;
  if (state != CalibreConnectState::SERVER_RUNNING) {
    const bool failed = state == CalibreConnectState::ERROR;
    ugly::notePage(renderer, mappedInput, tr(STR_CALIBRE_WIRELESS),
                   failed ? tr(STR_UGLY_CALIBRE_FAILED) : tr(STR_UGLY_CALIBRE_STARTING), nullptr, -1, hints);
    return;
  }
  using ugly::Size;
  constexpr int X = ugly::NOTE_X;
  const int room = renderer.getScreenWidth() - X - 30;
  ugly::notePaper(renderer, tr(STR_CALIBRE_WIRELESS));
  const std::string where = connectedSSID + "   " + tr(STR_IP_ADDRESS_PREFIX) + connectedIP;
  renderer.drawText(UI_10_FONT_ID, X, 100, renderer.truncatedText(UI_10_FONT_ID, where.c_str(), room).c_str());
  int y = 170;
  for (const StrId step : {StrId::STR_CALIBRE_INSTRUCTION_1, StrId::STR_CALIBRE_INSTRUCTION_2,
                           StrId::STR_CALIBRE_INSTRUCTION_3, StrId::STR_CALIBRE_INSTRUCTION_4})
    y += ugly::paragraph(renderer, Size::S22, X, y, room, 30, I18N.get(step)) * 30 + 6;
  y += 30;
  if (lastProgressTotal > 0 && lastProgressReceived <= lastProgressTotal) {
    ugly::text(renderer, Size::S30, X, y, tr(STR_UGLY_CALIBRE_RECEIVING));
    renderer.drawText(UI_10_FONT_ID, X, y + 12,
                      renderer.truncatedText(UI_10_FONT_ID, currentUploadName.c_str(), room).c_str());
    ugly::noteBar(renderer, y + 48,
                  static_cast<int>(static_cast<uint64_t>(lastProgressReceived) * 100 / lastProgressTotal));
    y += 48 + ugly::NOTE_BAR_H + 40;
  } else {
    ugly::paragraph(renderer, Size::S30, X, y, room, 40, tr(STR_UGLY_CALIBRE_WAITING));
    y += 90;
  }
  if (lastCompleteAt > 0 && (millis() - lastCompleteAt) < 6000) {
    const std::string msg = std::string(tr(STR_CALIBRE_RECEIVED)) + lastCompleteName;
    renderer.drawText(UI_10_FONT_ID, X, y, renderer.truncatedText(UI_10_FONT_ID, msg.c_str(), room).c_str());
  }
  ugly::statusBar(renderer, mappedInput, hints);
  renderer.displayBuffer();
#ifdef UGLY_FRAME_LOG
  LOG_INF("UGLY", "Calibre frame receiving=%d total=%lums heap=%u", lastProgressTotal > 0 ? 1 : 0,
          static_cast<unsigned long>(millis() - started), ESP.getFreeHeap());
#endif
}

void CalibreConnectActivity::render(RenderLock&&) {
  if (shell::uglyParts() && state != CalibreConnectState::WIFI_SELECTION) {
    renderUgly();
    return;
  }
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CALIBRE_WIRELESS));
  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - height) / 2;

  if (state == CalibreConnectState::SERVER_STARTING) {
    renderer.drawCenteredText(UI_12_FONT_ID, top, tr(STR_CALIBRE_STARTING));
  } else if (state == CalibreConnectState::ERROR) {
    renderer.drawCenteredText(UI_12_FONT_ID, top, tr(STR_CONNECTION_FAILED), true, EpdFontFamily::BOLD);
  } else if (state == CalibreConnectState::SERVER_RUNNING) {
    GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                      connectedSSID.c_str(), (std::string(tr(STR_IP_ADDRESS_PREFIX)) + connectedIP).c_str());

    int y = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing * 4;
    const auto heightText12 = renderer.getTextHeight(UI_12_FONT_ID);
    renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding, y, tr(STR_CALIBRE_SETUP), true, EpdFontFamily::BOLD);
    y += heightText12 + metrics.verticalSpacing * 2;

    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, y, tr(STR_CALIBRE_INSTRUCTION_1));
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, y + height, tr(STR_CALIBRE_INSTRUCTION_2));
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, y + height * 2, tr(STR_CALIBRE_INSTRUCTION_3));
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, y + height * 3, tr(STR_CALIBRE_INSTRUCTION_4));

    y += height * 3 + metrics.verticalSpacing * 4;
    renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding, y, tr(STR_CALIBRE_STATUS), true, EpdFontFamily::BOLD);
    y += heightText12 + metrics.verticalSpacing * 2;

    if (lastProgressTotal > 0 && lastProgressReceived <= lastProgressTotal) {
      std::string label = tr(STR_CALIBRE_RECEIVING);
      if (!currentUploadName.empty()) {
        label += ": " + currentUploadName;
        label = renderer.truncatedText(SMALL_FONT_ID, label.c_str(), pageWidth - metrics.contentSidePadding * 2,
                                       EpdFontFamily::REGULAR);
      }
      renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, y, label.c_str());
      GUI.drawProgressBar(renderer,
                          Rect{metrics.contentSidePadding, y + height + metrics.verticalSpacing,
                               pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
                          lastProgressReceived, lastProgressTotal);
      y += height + metrics.verticalSpacing * 2 + metrics.progressBarHeight;
    }

    if (lastCompleteAt > 0 && (millis() - lastCompleteAt) < 6000) {
      std::string msg = std::string(tr(STR_CALIBRE_RECEIVED)) + lastCompleteName;
      msg = renderer.truncatedText(SMALL_FONT_ID, msg.c_str(), pageWidth - metrics.contentSidePadding * 2,
                                   EpdFontFamily::REGULAR);
      renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, y, msg.c_str());
    }

    const auto labels = mappedInput.mapLabels(tr(STR_EXIT), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer();
}
