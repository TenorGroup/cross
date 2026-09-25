#include "CrossPointWebServerActivity.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstddef>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "DeviceName.h"
#include "FileTransferState.h"
#include "MappedInputManager.h"
#include "NetworkModeSelectionActivity.h"
#include "SilentRestart.h"
#include "UIFontTiers.h"
#include "WifiSelectionActivity.h"
#include "activities/network/CalibreConnectActivity.h"
#include "activities/reader/ReaderActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/QrUtils.h"
#include "util/TaskWatchdog.h"

namespace {
// AP Mode configuration
constexpr const char* AP_SSID_DEFAULT = "tenor-cross";
constexpr const char* AP_HOSTNAME_DEFAULT = "tenor-cross";

// The access-point SSID and the mDNS label both follow the user's device name,
// so a reader renamed in settings is recognisable from a phone's Wi-Fi list.
const char* apSsid() {
  static char buf[64];
  deviceNetworkName(buf, sizeof(buf), AP_SSID_DEFAULT);
  return buf;
}

const char* apHostname() {
  static char buf[64];
  deviceNetworkName(buf, sizeof(buf), AP_HOSTNAME_DEFAULT);
  return buf;
}
constexpr uint8_t AP_CHANNEL = 1;
constexpr uint8_t AP_MAX_CONNECTIONS = 4;
constexpr int QR_CODE_WIDTH = 198;
constexpr int QR_CODE_HEIGHT = 198;

// Network names are sanitised ASCII. Wrap addresses without hiding any byte
// behind an ellipsis; a fixed scratch line avoids allocating a list of lines.
int drawNetworkText(const GfxRenderer& renderer, const int font, const char* text, const int x, int y, const int width,
                    const bool centered = false) {
  const int lineHeight = renderer.getLineHeight(font);
  size_t remaining = strlen(text);
  while (remaining != 0) {
    char line[64];
    const size_t capacity = std::min(remaining, sizeof(line) - 1);
    memcpy(line, text, capacity);
    line[capacity] = '\0';
    size_t count = capacity;
    if (renderer.getTextWidth(font, line) > width) {
      size_t low = 1;
      size_t high = capacity;
      while (low < high) {
        const size_t mid = (low + high + 1) / 2;
        const char saved = line[mid];
        line[mid] = '\0';
        const bool fits = renderer.getTextWidth(font, line) <= width;
        line[mid] = saved;
        if (fits) {
          low = mid;
        } else {
          high = mid - 1;
        }
      }
      count = low;
      line[count] = '\0';
    }
    const int left = centered ? x + (width - renderer.getTextWidth(font, line)) / 2 : x;
    renderer.drawText(font, left, y, line);
    text += count;
    remaining -= count;
    y += lineHeight;
  }
  return y;
}

// DNS server for captive portal (redirects all DNS queries to our IP)
DNSServer* dnsServer = nullptr;
constexpr uint16_t DNS_PORT = 53;

void stopDnsServer() {
  if (!dnsServer) return;

  dnsServer->stop();
  delete dnsServer;
  dnsServer = nullptr;
}

void restartMdns(const char* hostname, const char* tag) {
  MDNS.end();
  if (MDNS.begin(hostname)) {
    LOG_DBG(tag, "mDNS started: http://%s.local/", hostname);
  } else {
    LOG_DBG(tag, "WARNING: mDNS failed to start");
  }
}

// 0..4 bars from RSSI (dBm), with 3 dBm hysteresis on currentBars to suppress flicker.
int barsForRssi(int rssi, int currentBars) {
  static constexpr int RISE_DBM[] = {-85, -75, -65, -55};
  static constexpr int FALL_DBM[] = {-88, -78, -68, -58};
  int bars = std::clamp(currentBars, 0, 4);
  while (bars < 4 && rssi >= RISE_DBM[bars]) bars++;
  while (bars > 0 && rssi < FALL_DBM[bars - 1]) bars--;
  return bars;
}
}  // namespace

void CrossPointWebServerActivity::onEnter() {
  runtimeStarted = false;
  // Claim the radio before any render, Wi-Fi startup, or nested activity work.
  if (!filetransfer::acquire()) {
    LOG_ERR("WEBACT", "BLE teardown incomplete; leaving file transfer");
    leave();
    return;
  }
  runtimeStarted = true;
  Activity::onEnter();

  LOG_INF("WEBACT", "Enter after reader closed heap=%u largest=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  // Heap-critical transition: WiFi (~45KB) plus the web server have to fit in
  // what's left of the ~380KB parts. SD-font caches retained for the CJK UI
  // fallback (mini glyph/kern arenas, kern class tables) are rebuildable on
  // demand - release them up front instead of aborting in startWebServer()
  // when the heap comes up short (observed on X3 with a Korean SD font).
  if (auto* fcm = renderer.getFontCacheManager()) {
    RenderLock lock(*this);
    fcm->releaseSdFontCaches();
    LOG_DBG("WEBACT", "Free heap after SD font cache release: %d bytes", ESP.getFreeHeap());
  }

  // Reset state
  state = WebServerActivityState::MODE_SELECTION;
  networkMode = NetworkMode::JOIN_NETWORK;
  isApMode = false;
  connectedIP.clear();
  connectedSSID.clear();
  lastHandleClientTime = 0;
  requestUpdate();

#ifdef TENOR_UI_ACCEPTANCE
  if (autoJoinForTest) {
    autoJoinForTest = false;
    onNetworkModeSelected(NetworkMode::JOIN_NETWORK);
    return;
  }
#endif

  // Launch network mode selection subactivity
  LOG_DBG("WEBACT", "Launching NetworkModeSelectionActivity...");
  startActivityForResult(std::make_unique<NetworkModeSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             leave();
                           } else {
                             onNetworkModeSelected(std::get<NetworkModeResult>(result.data).mode);
                           }
                         });
}

void CrossPointWebServerActivity::onExit() {
  const bool sampledBack = backLatch.active();
  backLatch.stop();
  if (sampledBack) LOG_INF("WEBACT", "Back sampler stopped stack_free=%u", backLatch.stackFreeBytes());
  Activity::onExit();

  // Sleep taken here ends a detour from the book. main saved the sleep origin
  // before closing this screen, so record it as a sleep from the reader: the
  // sleep screen then shows the book, as it would from inside it.
  if (!returnBook.empty() && activityManager.isSleepTransition()) {
    APP_STATE.lastSleepFromReader = true;
    APP_STATE.saveToFile();
  }

  if (runtimeStarted) {
    LOG_DBG("WEBACT", "Free heap at onExit start: %d bytes", ESP.getFreeHeap());

    state = WebServerActivityState::SHUTTING_DOWN;
    stopDnsServer();
    MDNS.end();

    // Skip reboot if WiFi was never activated (e.g. user backed out of mode selection).
    if (WiFi.getMode() != WIFI_MODE_NULL) {
      if (isApMode) {
        WiFi.softAPdisconnect(true);
      } else {
        WiFi.disconnect(false);
      }
      delay(30);
      // The restart hands back the heap Wi-Fi fragmented; the reader then opens
      // on a clean heap, the same way it would after a cold boot.
      if (toBook) {
        silentRestartToReader();
      } else {
        silentRestart();
      }
    }

    LOG_DBG("WEBACT", "Free heap at onExit end: %d bytes", ESP.getFreeHeap());
  }
  // Release only after DNS/mDNS and Wi-Fi teardown have completed. A nested
  // activity may still hold its own owner while this activity exits.
  filetransfer::release();
}

void CrossPointWebServerActivity::onNetworkModeSelected(const NetworkMode mode) {
  const char* modeName = "Join Network";
  if (mode == NetworkMode::CONNECT_CALIBRE) {
    modeName = "Connect to Calibre";
  } else if (mode == NetworkMode::CREATE_HOTSPOT) {
    modeName = "Create Hotspot";
#if FREEINK_CAP_USB_MSC
  } else if (mode == NetworkMode::USB_DRIVE) {
    modeName = "USB Drive";
#endif
  }
  LOG_DBG("WEBACT", "Network mode selected: %s", modeName);

#if FREEINK_CAP_USB_MSC
  if (mode == NetworkMode::USB_DRIVE) {
    activityManager.goToUsbDrive();
    return;
  }
#endif

  networkMode = mode;
  isApMode = (mode == NetworkMode::CREATE_HOTSPOT);

  if (mode == NetworkMode::CONNECT_CALIBRE) {
    startActivityForResult(
        std::make_unique<CalibreConnectActivity>(renderer, mappedInput), [this](const ActivityResult& result) {
          state = WebServerActivityState::MODE_SELECTION;

          startActivityForResult(std::make_unique<NetworkModeSelectionActivity>(renderer, mappedInput),
                                 [this](const ActivityResult& result) {
                                   if (result.isCancelled) {
                                     leave();
                                   } else {
                                     onNetworkModeSelected(std::get<NetworkModeResult>(result.data).mode);
                                   }
                                 });
        });
    return;
  }

  if (mode == NetworkMode::JOIN_NETWORK) {
    // STA mode - launch WiFi selection
    LOG_DBG("WEBACT", "Turning on WiFi (STA mode)...");
    WiFi.mode(WIFI_STA);

    state = WebServerActivityState::WIFI_SELECTION;
    LOG_DBG("WEBACT", "Launching WifiSelectionActivity...");
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
    // AP mode - start access point
    state = WebServerActivityState::AP_STARTING;
    requestUpdate();
    startAccessPoint();
  }
}

void CrossPointWebServerActivity::onWifiSelectionComplete(const bool connected) {
  LOG_DBG("WEBACT", "WifiSelectionActivity completed, connected=%d", connected);

  if (connected) {
    // Get connection info before exiting subactivity
    isApMode = false;

    LOG_INF("WEBACT", "mDNS begin");
    // Start mDNS for hostname resolution
    restartMdns(apHostname(), "WEBACT");

    // Start the web server
    startWebServer();
  } else {
    // User cancelled - go back to mode selection
    state = WebServerActivityState::MODE_SELECTION;

    startActivityForResult(std::make_unique<NetworkModeSelectionActivity>(renderer, mappedInput),
                           [this](const ActivityResult& result) {
                             if (result.isCancelled) {
                               leave();
                             } else {
                               onNetworkModeSelected(std::get<NetworkModeResult>(result.data).mode);
                             }
                           });
  }
}

void CrossPointWebServerActivity::startAccessPoint() {
  LOG_DBG("WEBACT", "Starting Access Point mode...");
  LOG_DBG("WEBACT", "Free heap before AP start: %d bytes", ESP.getFreeHeap());

  // Configure and start the AP
  WiFi.mode(WIFI_AP);
  delay(100);

  // Open network: the transfer session is meant to be joined without typing
  // anything, on either the reader or the client, so the hotspot carries no key.
  const bool apStarted = WiFi.softAP(apSsid(), nullptr, AP_CHANNEL, false, AP_MAX_CONNECTIONS);

  if (!apStarted) {
    LOG_ERR("WEBACT", "ERROR: Failed to start Access Point!");
    leave();
    return;
  }

  delay(100);  // Wait for AP to fully initialize

  // Get AP IP address
  const IPAddress apIP = WiFi.softAPIP();
  char ipStr[16];
  snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", apIP[0], apIP[1], apIP[2], apIP[3]);
  connectedIP = ipStr;
  connectedSSID = apSsid();

  LOG_DBG("WEBACT", "Access Point started!");
  LOG_DBG("WEBACT", "SSID: %s", apSsid());
  LOG_DBG("WEBACT", "IP: %s", connectedIP.c_str());

  // Start mDNS for hostname resolution
  restartMdns(apHostname(), "WEBACT");

  // Start DNS server for captive portal behavior
  // This redirects all DNS queries to our IP, making any domain typed resolve to us
  stopDnsServer();
  dnsServer = new DNSServer();
  dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer->start(DNS_PORT, "*", apIP);
  LOG_DBG("WEBACT", "DNS server started for captive portal");

  LOG_DBG("WEBACT", "Free heap after AP start: %d bytes", ESP.getFreeHeap());

  // Start the web server
  startWebServer();
}

void CrossPointWebServerActivity::startWebServer() {
  LOG_INF("WEBACT", "Server begin heap=%u largest=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  // Repeat the release right before the allocation: the WiFi selection screen
  // rendered since onEnter(), and a CJK SSID repopulates the SD-font caches.
  if (auto* fcm = renderer.getFontCacheManager()) {
    LOG_DBG("WEBACT", "Free heap before SD font cache release: %d bytes", ESP.getFreeHeap());
    {
      RenderLock lock(*this);
      fcm->releaseSdFontCaches();
    }
    LOG_DBG("WEBACT", "Free heap before server alloc: %d bytes", ESP.getFreeHeap());
  }

  // Create the web server instance
  if (!webServer) webServer = makeUniqueNoThrow<CrossPointWebServer>();
  if (!webServer) {
    leave();
    return;
  }
  webServer->setUiTextSizeApplier([this](const uint8_t size) {
    RenderLock lock(*this);
    if (!applyUiFontSize(renderer, size)) return false;
    SETTINGS.uiTextSize = size;
    UITheme::getInstance().reload();
    requestUpdate();
    return true;
  });
  // A slow or stalled upload keeps handleClient() busy for as long as bytes trickle in;
  // a Back tap latched meanwhile ends it instead of waiting behind it.
  webServer->setUploadCancel([this] { return backLatch.latched(); });
  webServer->begin();

  if (webServer->isRunning()) {
    if (!backLatch.start(gpio, mappedInput.physicalBack())) {
      LOG_ERR("WEBACT", "Cannot start Back sampler");
      leave();
      return;
    }
    state = WebServerActivityState::SERVER_RUNNING;
    LOG_INF("WEBACT", "Server ready heap=%u largest=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    lastWifiBars = isApMode ? 0 : barsForRssi(WiFi.RSSI(), 0);

    // Force an immediate render since we're transitioning from a subactivity
    // that had its own rendering task. We need to make sure our display is shown.
    requestUpdate();
  } else {
    LOG_ERR("WEBACT", "ERROR: Failed to start web server!");
    webServer.reset();
    // Go back on error
    leave();
  }
}

// Opened from a book, every exit but the Home gesture reopens that book on its
// saved page. A book deleted or moved away during the session has no file left
// at its path, and a reader that cannot be allocated has nothing to show: Home.
void CrossPointWebServerActivity::leave() {
  if (!returnBook.empty() && Storage.exists(returnBook.c_str())) {
    auto reader = ReaderActivity::create(renderer, mappedInput, returnBook, false);
    if (reader) {
      toBook = true;
      activityManager.replaceActivity(std::move(reader));
      return;
    }
  }
  onGoHome();
}

void CrossPointWebServerActivity::stopServerAndLeave() {
  state = WebServerActivityState::SHUTTING_DOWN;
  backLatch.stop();
  stopDnsServer();
  if (webServer) {
    webServer->stop();
    webServer.reset();
  }
  leave();
}

void CrossPointWebServerActivity::loop() {
  // Handle different states
  if (state == WebServerActivityState::SERVER_RUNNING) {
    // Main already sampled input for this pass. Preserve that release edge and
    // leave before starting another request; teardown runs via ActivityManager.
    const bool homeGesture = mappedInput.wasHomeGesture();
    if (backLatch.consume() ||
        (!backLatch.active() && mappedInput.wasReleased(MappedInputManager::Button::Back)) || homeGesture) {
      // The Home gesture asks for Home, even when the session came from a book.
      if (homeGesture) returnBook.clear();
      stopServerAndLeave();
      return;
    }

    // Handle DNS requests for captive portal (AP mode only)
    if (isApMode && dnsServer) {
      dnsServer->processNextRequest();
    }

    // STA mode: Monitor WiFi connection health
    if (!isApMode && webServer && webServer->isRunning()) {
      static unsigned long lastWifiCheck = 0;
      if (millis() - lastWifiCheck > 2000) {  // Check every 2 seconds
        lastWifiCheck = millis();
        const wl_status_t wifiStatus = WiFi.status();
        // Driver auto-reconnect handles retries; abandon (via onGoHome) only
        // after WIFI_ABANDON_MS, otherwise the activity freezes on a blip.
        bool repaint = false;
        if (wifiStatus != WL_CONNECTED) {
          if (consecutiveDisconnects == 0) {
            firstDisconnectAt = millis();
            repaint = true;
          }
          consecutiveDisconnects++;
          LOG_DBG("WEBACT", "WiFi not connected (status=%d, consecutive=%d, total=%lu ms)", wifiStatus,
                  consecutiveDisconnects, millis() - firstDisconnectAt);
          if (millis() - firstDisconnectAt > WIFI_ABANDON_MS) {
            LOG_DBG("WEBACT", "WiFi unavailable for >%lu s; returning to network selection", WIFI_ABANDON_MS / 1000UL);
            stopServerAndLeave();
            return;
          }
        } else {
          if (consecutiveDisconnects > 0) {
            LOG_DBG("WEBACT", "WiFi recovered after %d failed checks (%lu ms)", consecutiveDisconnects,
                    millis() - firstDisconnectAt);
            repaint = true;
          }
          consecutiveDisconnects = 0;
          firstDisconnectAt = 0;
          const int rssi = WiFi.RSSI();
          if (rssi < -75) {
            LOG_DBG("WEBACT", "Warning: Weak WiFi signal: %d dBm", rssi);
          }
          const int bars = barsForRssi(rssi, lastWifiBars);
          if (bars != lastWifiBars) {
            lastWifiBars = bars;
            repaint = true;
          }
        }
        if (repaint) requestUpdate();
      }
    }

    // Handle web server requests with watchdog safety.
    if (webServer && webServer->isRunning()) {
      const unsigned long timeSinceLastHandleClient = millis() - lastHandleClientTime;

      // Log if there's a significant gap between handleClient calls (>100ms)
      if (lastHandleClientTime > 0 && timeSinceLastHandleClient > 100) {
        LOG_DBG("WEBACT", "WARNING: %lu ms gap since last handleClient", timeSinceLastHandleClient);
      }

      // Reset watchdog BEFORE processing - HTTP header parsing can be slow
      resetTaskWatchdogIfSubscribed();

      // Service one request pass, then let main sample input again. skipLoopDelay()
      // keeps the next pass immediate while the server is running. A synchronous
      // handler finishes before ActivityManager can tear down the server.
      webServer->handleClient();
      lastHandleClientTime = millis();
      if (backLatch.consume()) {
        stopServerAndLeave();
        return;
      }
      // End an idle transfer session only after the current handler has
      // returned. The server policy keeps active transfers alive.
      if (webServer->sessionIdleExpired(millis())) {
        LOG_INF("WEBACT", "File transfer idle timeout; closing server");
        stopServerAndLeave();
        return;
      }
    }
  }
}

void CrossPointWebServerActivity::render(RenderLock&&) {
  // Only render our own UI when server is running
  // Subactivities handle their own rendering
  if (state == WebServerActivityState::SERVER_RUNNING || state == WebServerActivityState::AP_STARTING) {
    renderer.clearScreen();
    const auto& metrics = UITheme::getInstance().getMetrics();
    const auto pageWidth = renderer.getScreenWidth();
    const auto pageHeight = renderer.getScreenHeight();

    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   isApMode ? tr(STR_HOTSPOT_MODE) : tr(STR_FILE_TRANSFER), nullptr);

    if (state == WebServerActivityState::SERVER_RUNNING) {
      GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                        connectedSSID.c_str());
      renderServerRunning();
    } else {
      const auto height = renderer.getLineHeight(UI_10_FONT_ID);
      const auto top = (pageHeight - height) / 2;
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_STARTING_HOTSPOT));
    }
    renderer.displayBuffer();
  }
}

void CrossPointWebServerActivity::renderServerRunning() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 isApMode ? tr(STR_HOTSPOT_MODE) : tr(STR_FILE_TRANSFER), nullptr);
  GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                    connectedSSID.c_str());

  if (!isApMode) {
    renderWifiIndicator(metrics.topPadding + metrics.headerHeight);
  }

  // Version-4 QR codes have 6 px modules here. Leave four modules clear on
  // either side, including between the code and the larger address text.
  constexpr int qrQuiet = 24;
  const int left = std::max<int>(metrics.contentSidePadding, qrQuiet);
  const int bodyWidth = pageWidth - 2 * left;
  int startY = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + qrQuiet;
  const int height10 = renderer.getLineHeight(UI_10_FONT_ID);
  const std::string hostnameUrl = std::string("http://") + apHostname() + ".local/";
  const std::string ipUrl = "http://" + connectedIP + "/";
  const std::string apDisplayIp = "http://" + connectedIP;
  if (isApMode) {
    const int textX = left + QR_CODE_WIDTH + qrQuiet;
    const int textWidth = pageWidth - left - textX;
    // Keep each instruction beside its QR code so both steps fit at large UI sizes.
    const std::string wifiConfig = std::string("WIFI:T:nopass;S:") + connectedSSID + ";;";
    const Rect qrBoundsWifi(left, startY, QR_CODE_WIDTH, QR_CODE_HEIGHT);
    QrUtils::drawQrCode(renderer, qrBoundsWifi, wifiConfig);
    renderer.drawText(UI_10_FONT_ID, textX, startY, tr(STR_CONNECT_WIFI_HINT), true, EpdFontFamily::BOLD);
    const int nameBottom =
        drawNetworkText(renderer, UI_12_FONT_ID, connectedSSID.c_str(), textX, startY + height10 + 8, textWidth);
    startY = std::max(startY + QR_CODE_HEIGHT, nameBottom) + 2 * qrQuiet;

    const Rect qrBoundsUrl(left, startY, QR_CODE_WIDTH, QR_CODE_HEIGHT);
    QrUtils::drawQrCode(renderer, qrBoundsUrl, hostnameUrl);
    renderer.drawText(UI_10_FONT_ID, textX, startY, tr(STR_OPEN_URL_HINT), true, EpdFontFamily::BOLD);
    drawNetworkText(renderer, UI_12_FONT_ID, apDisplayIp.c_str(), textX, startY + height10 + 8, textWidth);
  } else {
    renderer.drawCenteredText(UI_10_FONT_ID, startY, tr(STR_OPEN_URL_HINT), true, EpdFontFamily::BOLD);
    startY += height10;
    renderer.drawCenteredText(UI_10_FONT_ID, startY, tr(STR_SCAN_QR_HINT), true, EpdFontFamily::BOLD);
    startY += height10 + qrQuiet;

    const Rect qrBounds((pageWidth - QR_CODE_WIDTH) / 2, startY, QR_CODE_WIDTH, QR_CODE_HEIGHT);
    QrUtils::drawQrCode(renderer, qrBounds, ipUrl);
    startY += QR_CODE_HEIGHT + qrQuiet;
    startY = drawNetworkText(renderer, UI_12_FONT_ID, ipUrl.c_str(), left, startY, bodyWidth, true);
    drawNetworkText(renderer, UI_12_FONT_ID, hostnameUrl.c_str(), left, startY + 12, bodyWidth, true);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_EXIT), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void CrossPointWebServerActivity::renderWifiIndicator(int subHeaderTop) const {
  constexpr int BAR_COUNT = 4;
  constexpr int BAR_WIDTH = 4;
  constexpr int BAR_GAP = 2;
  constexpr int ICON_HEIGHT = 14;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int iconWidth = BAR_COUNT * BAR_WIDTH + (BAR_COUNT - 1) * BAR_GAP;
  const int iconRight = renderer.getScreenWidth() - metrics.contentSidePadding;
  const int iconLeft = iconRight - iconWidth;
  const int iconBottom = subHeaderTop + metrics.tabBarHeight - metrics.verticalSpacing;

  const bool wifiUp = (WiFi.status() == WL_CONNECTED) && (consecutiveDisconnects == 0);
  if (wifiUp) {
    for (int i = 0; i < BAR_COUNT; i++) {
      const int barHeight = (i + 1) * ICON_HEIGHT / BAR_COUNT;
      const int x = iconLeft + i * (BAR_WIDTH + BAR_GAP);
      const int y = iconBottom - barHeight;
      if (i < lastWifiBars) {
        renderer.fillRect(x, y, BAR_WIDTH, barHeight, true);
      } else {
        renderer.drawRect(x, y, BAR_WIDTH, barHeight, true);
      }
    }
  } else {
    const int xSize = ICON_HEIGHT;
    const int x0 = iconRight - xSize;
    const int y0 = iconBottom - xSize;
    renderer.drawLine(x0, y0, x0 + xSize, y0 + xSize, 2, true);
    renderer.drawLine(x0, y0 + xSize, x0 + xSize, y0, 2, true);
  }
}
