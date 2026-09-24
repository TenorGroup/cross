#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#define LOG_DBG(...) ((void)0)
#define LOG_INF(...) ((void)0)

static unsigned long nowMs = 1000;
static int watchdogResets = 0;
static int serverCalls = 0;
static int serverStops = 0;
static int expiryChecks = 0;
static int dnsStops = 0;
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

// InputManager::update clears releasedEvents before polling GPIO. A release
// sampled by main survives until the next update, which can erase it.
struct MappedInputManager {
  enum class Button { Back };
  bool released = false;
  bool home = false;
  bool pendingRelease = false;
  int updates = 0;
  void update() {
    ++updates;
    released = pendingRelease;
    pendingRelease = false;
    home = false;
  }
  bool wasReleased(Button) const { return released; }
  bool wasHomeGesture() const { return home; }
};

struct FakeServer {
  bool running = true;
  bool inHandler = false;
  bool expired = false;
  int calls = 0;
  unsigned long handlerMs = 0;
  std::function<void()> duringHandler;
  bool isRunning() const { return running; }
  void handleClient() {
    inHandler = true;
    ++calls;
    ++serverCalls;
    nowMs += handlerMs;
    if (duringHandler) duringHandler();
    inHandler = false;
  }
  bool sessionIdleExpired(unsigned long) {
    ++expiryChecks;
    return expired;
  }
  void stop() {
    if (inHandler) throw std::runtime_error("server stopped inside HTTP handler");
    running = false;
    ++serverStops;
  }
};
enum class WebServerActivityState { SERVER_RUNNING, SHUTTING_DOWN, AP_STARTING };
struct CrossPointWebServerActivity {
  WebServerActivityState state = WebServerActivityState::SERVER_RUNNING;
  bool isApMode = true;
  std::unique_ptr<FakeServer> webServer = std::make_unique<FakeServer>();
  MappedInputManager mappedInput;
  struct FakeLatch {
    int stops = 0;
    bool consume() const { return false; }
    bool active() const { return false; }
    void stop() { ++stops; }
  } backLatch;
  unsigned long lastHandleClientTime = 0;
  unsigned long firstDisconnectAt = 0;
  static constexpr unsigned long WIFI_ABANDON_MS = 300000;
  int consecutiveDisconnects = 0;
  int lastWifiBars = 3;
  int paints = 0;
  int exits = 0;
  // Every session here opens outside a book, so each exit lands on Home.
  std::string returnBook;
  void requestUpdate() { ++paints; }
  void onGoHome() {
    if (webServer && webServer->inHandler) throw std::runtime_error("exit inside HTTP handler");
    ++exits;
  }
  void leave() {
    if (!returnBook.empty()) throw std::runtime_error("session outside a book asked for a book");
    onGoHome();
  }
  void stopServerAndLeave() {
    state = WebServerActivityState::SHUTTING_DOWN;
    backLatch.stop();
    stopDnsServer();
    if (webServer) {
      webServer->stop();
      webServer.reset();
    }
    leave();
  }
  void loop();
#include "production-delay.inc"
};
#include "production-loop.inc"

void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

void run(const std::string& name) {
  CrossPointWebServerActivity activity;
  auto& input = activity.mappedInput;
  auto& server = *activity.webServer;
  if (name == "entry-back" || name == "entry-home") {
    input.released = name == "entry-back";
    input.home = name == "entry-home";
    activity.loop();
    require(activity.exits == 1, "main-sampled exit event was lost");
    require(serverCalls == 0 && dns.calls == 0, "exit delayed behind network service");
    require(input.updates == 0, "activity overwrote main input frame");
    require(!activity.webServer && serverStops == 1 && dnsStops == 1 && activity.backLatch.stops == 1,
            "entry exit skipped synchronous cleanup");
  } else if (name == "loaded-handler-budget") {
    server.handlerMs = 20;
    auto start = millis();
    activity.loop();
    std::cout << "loaded pass elapsed_ms=" << millis() - start << " calls=" << server.calls << '\n';
    require(millis() - start <= 20, "handler batch starved the next main input sample");
    require(server.calls == 1 && watchdogResets > 0, "network service/watchdog missing");
  } else if (name == "release-after-handler" || name == "upload-boundary-exit") {
    bool uploadCommitted = false;
    server.handlerMs = 20;
    if (name == "upload-boundary-exit") {
      server.duringHandler = [&] {
        input.pendingRelease = true;
        uploadCommitted = true;
      };
    }
    activity.loop();
    require(activity.exits == 0, "exit must await main input sampling");
    if (name == "upload-boundary-exit") {
      require(uploadCommitted, "in-flight upload handler did not complete");
    } else {
      input.pendingRelease = true;  // The user releases just after the handler.
    }
    int finishedCalls = serverCalls;
    input.update();  // The next main pass samples the physical release.
    activity.loop();
    require(activity.exits == 1, "first release after HTTP handler did not exit");
    require(serverCalls == finishedCalls, "another request ran after the exit event");
  } else if (name == "ap-dns-and-throughput") {
    for (int pass = 0; pass < 100; ++pass) {
      input.update();
      activity.loop();
      require(activity.skipLoopDelay(), "main inserted a delay while the server was running");
    }
    require(server.calls >= 100 && dns.calls == 100, "HTTP or captive DNS service was skipped");
    require(input.updates == 100, "input sampled outside the main loop");
  } else if (name == "wifi-recovery") {
    activity.isApMode = false;
    WiFi.connection = WL_DISCONNECTED;
    nowMs = 3001;
    activity.loop();
    require(activity.consecutiveDisconnects == 1 && activity.paints == 1, "disconnect was not tracked");
    WiFi.connection = WL_CONNECTED;
    nowMs += 2001;
    activity.loop();
    require(activity.consecutiveDisconnects == 0 && activity.firstDisconnectAt == 0,
            "recovery did not clear outage tracking");
    require(activity.paints == 2 && activity.exits == 0, "recovery exit/repaint regression");
  } else if (name == "wifi-abandon") {
    activity.isApMode = false;
    WiFi.connection = WL_DISCONNECTED;
    nowMs = 3001;
    activity.loop();
    int priorCalls = serverCalls;
    nowMs += activity.WIFI_ABANDON_MS + 1;
    activity.loop();
    require(activity.exits == 1 && activity.state == WebServerActivityState::SHUTTING_DOWN,
            "sustained outage did not leave transfer");
    require(serverCalls == priorCalls, "request processed after abandoning WiFi");
  } else if (name == "stopped-server-back") {
    server.running = false;
    input.released = true;
    activity.loop();
    require(activity.exits == 1 && serverCalls == 0, "stopped server trapped Back");
    require(!activity.skipLoopDelay(), "stopped server kept main busy");
  } else if (name == "idle-timeout-safe-boundary") {
    bool handlerCompleted = false;
    server.expired = true;
    server.duringHandler = [&] { handlerCompleted = true; };
    activity.loop();
    require(handlerCompleted && serverCalls == 1 && expiryChecks == 1,
            "idle timeout did not wait for the request boundary");
    require(activity.exits == 1 && !activity.webServer, "idle timeout did not leave transfer");
    require(serverStops == 1 && dnsStops == 1 && activity.backLatch.stops == 1,
            "idle timeout skipped synchronous cleanup");
  } else if (name == "recent-transfer-activity-defers-timeout") {
    activity.loop();
    require(activity.exits == 0 && activity.webServer && serverCalls == 1 && expiryChecks == 1,
            "recent transfer activity was cut by idle timeout");
    server.expired = true;
    activity.loop();
    require(activity.exits == 1 && !activity.webServer && serverCalls == 2 && expiryChecks == 2,
            "stalled transfer session did not close at its idle deadline");
  } else if (name == "inactive-state") {
    activity.state = WebServerActivityState::AP_STARTING;
    input.released = true;
    activity.loop();
    require(activity.exits == 0 && server.calls == 0 && dns.calls == 0,
            "inactive transition behavior changed");
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
