#pragma once
#include "activities/X3ActivityLayout.h"

#include <functional>
#include <memory>
#include <string>

#include "NetworkModeSelectionActivity.h"
#include "activities/Activity.h"
#include "network/CrossPointWebServer.h"
#include "util/FileTransferBackLatch.h"

// Web server activity states
enum class WebServerActivityState {
  MODE_SELECTION,  // Choosing between Join Network and Create Hotspot
  WIFI_SELECTION,  // WiFi selection subactivity is active (for Join Network mode)
  AP_STARTING,     // Starting Access Point mode
  SERVER_RUNNING,  // Web server is running and handling requests
  SHUTTING_DOWN    // Shutting down server and WiFi
};

/**
 * CrossPointWebServerActivity is the entry point for file transfer functionality.
 * It:
 * - First presents a choice between "Join a Network" (STA), "Connect to Calibre", and "Create Hotspot" (AP)
 * - For STA mode: Launches WifiSelectionActivity to connect to an existing network
 * - For AP mode: Creates an Access Point that clients can connect to
 * - Starts the CrossPointWebServer when connected
 * - Handles client requests in its loop() function
 * - Cleans up the server and shuts down WiFi on exit
 */
class CrossPointWebServerActivity final : public Activity {
  x3activity::InfoPages x3Info;
  int x3Step = 0;

  WebServerActivityState state = WebServerActivityState::MODE_SELECTION;

  // Network mode
  NetworkMode networkMode = NetworkMode::JOIN_NETWORK;
  bool isApMode = false;
  bool runtimeStarted = false;
  // Book this session was opened from; empty when opened outside a book.
  std::string returnBook;
  bool openFontStore = false;  // opened out of a font store restart: go straight into the store
  // Set once the exit reopens returnBook, so a Wi-Fi restart boots into it.
  bool toBook = false;
#if defined(TENOR_UI_ACCEPTANCE) || defined(TENOR_X3_CARD_PROBE)
  bool autoJoinForTest = false;
#endif
  // Set when re-entered after the heap-defrag reboot: skip mode selection, go
  // straight to Join Network, and don't reboot again.
  bool startInJoinNetwork = false;

  // Web server - owned by this activity
  std::unique_ptr<CrossPointWebServer> webServer;
  FileTransferBackLatch backLatch;

  // Server status
  std::string connectedIP;
  std::string connectedSSID;  // For STA mode: network name, For AP mode: AP name

  // Performance monitoring
  unsigned long lastHandleClientTime = 0;

  // Sustained WiFi-loss tracking; abandon only after WIFI_ABANDON_MS.
  int consecutiveDisconnects = 0;
  unsigned long firstDisconnectAt = 0;
  static constexpr unsigned long WIFI_ABANDON_MS = 5UL * 60UL * 1000UL;

  // Cached signal-strength bracket (0..4) for the WiFi indicator.
  int lastWifiBars = 0;

  void renderServerRunning() const;
  void renderUgly() const;
  void renderWifiIndicator(int subHeaderTop) const;

  void chooseMode();
  void onNetworkModeSelected(NetworkMode mode);
  void onWifiSelectionComplete(bool connected);
  void leave();
  void stopServerAndLeave();
  void startAccessPoint();
  void startWebServer();

 public:
  tenorchrome::X3PathIcon navigationIcon() const override { return tenorchrome::X3PathIcon::Network; }
  std::string x3NavigationLabel() const override { return I18N.get(isApMode ? StrId::STR_HOTSPOT_MODE : StrId::STR_FILE_TRANSFER); }
#if defined(TENOR_UI_ACCEPTANCE) || defined(TENOR_X3_CARD_PROBE)
  void requestAutoJoinForTest() { autoJoinForTest = true; }
#endif
  explicit CrossPointWebServerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       std::string returnBook = {}, const bool openFontStore = false,
                                       const bool startInJoinNetwork = false)
      : Activity("CrossPointWebServer", renderer, mappedInput),
        returnBook(std::move(returnBook)),
        openFontStore(openFontStore),
        startInJoinNetwork(startInJoinNetwork) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return webServer && webServer->isRunning(); }
  bool preventAutoSleep() override { return webServer && webServer->isRunning(); }
  // Where this session stands for the wake: its mode (the font store, the server, Calibre, the hotspot...) and the
  // book it is a detour from. The screens it opens (the font store, Wi-Fi, Calibre) do not answer: this one does for them.
  bool wakePlace(WakePlace& place) const override {
    place.kind = WakeKind::Connect;
    place.row = static_cast<int16_t>(networkMode);
    place.path = returnBook;
    return true;
  }
};
