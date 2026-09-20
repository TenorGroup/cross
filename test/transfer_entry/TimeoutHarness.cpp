#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#define LOG_DBG(...) ((void)0)
#define LOG_INF(...) ((void)0)

static unsigned long nowMs = 1000;
static int watchdogResets = 0;
static int serverStops = 0;
static int dnsStops = 0;
static int serverCalls = 0;
static int expiryChecks = 0;
unsigned long millis() { return nowMs; }
void yield() {}
void resetTaskWatchdogIfSubscribed() { ++watchdogResets; }

enum wl_status_t { WL_DISCONNECTED, WL_CONNECTED };
struct FakeWifi {
  wl_status_t connection = WL_CONNECTED;
  wl_status_t status() const { return connection; }
  int RSSI() const { return -60; }
} WiFi;
int barsForRssi(int, int) { return 3; }

struct FakeDns {
  int calls = 0;
  void processNextRequest() { ++calls; }
};
static FakeDns dns;
static FakeDns* dnsServer = &dns;
void stopDnsServer() {
  if (!dnsServer) return;
  ++dnsStops;
  dnsServer = nullptr;
}

struct MappedInputManager {
  enum class Button { Back };
  bool released = false;
  bool home = false;
  bool wasReleased(Button) const { return released; }
  bool wasHomeGesture() const { return home; }
};

struct FakeServer {
  bool running = true;
  bool expired = false;
  bool expireDuringHandler = false;
  bool inHandler = false;
  int calls = 0;
  int expiryChecks = 0;

  bool isRunning() const { return running; }
  void handleClient() {
    inHandler = true;
    ++calls;
    ++serverCalls;
    if (expireDuringHandler) expired = true;
    inHandler = false;
  }
  bool sessionIdleExpired(unsigned long) {
    ++expiryChecks;
    ++::expiryChecks;
    return expired;
  }
  void stop() {
    if (inHandler) throw std::runtime_error("server stopped inside handler");
    running = false;
    ++serverStops;
  }
};

struct FakeLatch {
  bool isActive = true;
  bool pending = false;
  int stops = 0;
  bool consume() {
    const bool value = pending;
    pending = false;
    return value;
  }
  bool active() const { return isActive; }
  void stop() {
    isActive = false;
    ++stops;
  }
};

enum class WebServerActivityState { SERVER_RUNNING, SHUTTING_DOWN, AP_STARTING };
struct CrossPointWebServerActivity {
  WebServerActivityState state = WebServerActivityState::SERVER_RUNNING;
  bool isApMode = true;
  std::unique_ptr<FakeServer> webServer = std::make_unique<FakeServer>();
  MappedInputManager mappedInput;
  FakeLatch backLatch;
  unsigned long lastHandleClientTime = 0;
  unsigned long firstDisconnectAt = 0;
  static constexpr unsigned long WIFI_ABANDON_MS = 300000;
  int consecutiveDisconnects = 0;
  int lastWifiBars = 3;
  int paints = 0;
  int exits = 0;

  void requestUpdate() { ++paints; }
  void onGoHome() {
    if (webServer && webServer->inHandler) throw std::runtime_error("exit inside HTTP handler");
    ++exits;
  }
  void stopServerAndGoHome() {
    state = WebServerActivityState::SHUTTING_DOWN;
    backLatch.stop();
    stopDnsServer();
    if (webServer) {
      webServer->stop();
      webServer.reset();
    }
    onGoHome();
  }
  void loop();
};

#include "production-loop.inc"

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void resetGlobals() {
  nowMs = 1000;
  watchdogResets = 0;
  serverStops = 0;
  dnsStops = 0;
  serverCalls = 0;
  expiryChecks = 0;
  dnsServer = &dns;
  dns.calls = 0;
  WiFi.connection = WL_CONNECTED;
}

void requireCleanExit(const CrossPointWebServerActivity& activity) {
  require(activity.exits == 1, "activity did not exit exactly once");
  require(activity.state == WebServerActivityState::SHUTTING_DOWN, "activity did not enter shutdown state");
  require(!activity.webServer, "server owner survived exit");
  require(serverStops == 1, "server was not stopped exactly once");
  require(activity.backLatch.stops == 1, "Back latch was not stopped exactly once");
  require(dnsStops == 1 && dnsServer == nullptr, "DNS server was not stopped");
}

void run(const std::string& name) {
  resetGlobals();
  CrossPointWebServerActivity activity;
  FakeServer* server = activity.webServer.get();

  if (name == "idle-timeout") {
    server->expired = true;
    activity.loop();
    require(serverCalls == 1, "timeout was checked before the request boundary");
    require(expiryChecks == 1, "timeout policy was not checked after the request boundary");
    requireCleanExit(activity);
  } else if (name == "active-session") {
    activity.loop();
    require(activity.exits == 0 && activity.webServer, "active session was closed");
    require(serverCalls == 1 && expiryChecks == 1, "active session skipped service or timeout check");
    require(serverStops == 0 && activity.backLatch.stops == 0, "active session ran cleanup");
  } else if (name == "expires-during-handler") {
    server->expireDuringHandler = true;
    activity.loop();
    require(serverCalls == 1 && expiryChecks == 1, "handler boundary was not completed before timeout");
    requireCleanExit(activity);
  } else if (name == "back-before-handler") {
    activity.backLatch.pending = true;
    activity.loop();
    require(serverCalls == 0 && expiryChecks == 0, "Back waited behind a request");
    requireCleanExit(activity);
  } else {
    throw std::runtime_error("unknown scenario");
  }
}

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    run(argv[1]);
    std::cout << "PASS " << argv[1] << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << argv[1] << ": " << error.what() << '\n';
    return 1;
  }
}
