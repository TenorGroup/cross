#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#define LOG_DBG(...) ((void)0)

static unsigned long nowMs = 1000;
static int watchdogResets = 0;
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
  int calls = 0;
  unsigned long handlerMs = 0;
  std::function<void()> duringHandler;
  bool isRunning() const { return running; }
  void handleClient() {
    inHandler = true;
    ++calls;
    nowMs += handlerMs;
    if (duringHandler) duringHandler();
    inHandler = false;
  }
};
enum class WebServerActivityState { SERVER_RUNNING, SHUTTING_DOWN, AP_STARTING };
struct CrossPointWebServerActivity {
  WebServerActivityState state = WebServerActivityState::SERVER_RUNNING;
  bool isApMode = true;
  std::unique_ptr<FakeServer> webServer = std::make_unique<FakeServer>();
  MappedInputManager mappedInput;
  struct {
    bool consume() const { return false; }
    bool active() const { return false; }
  } backLatch;
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
    require(server.calls == 0 && dns.calls == 0, "exit delayed behind network service");
    require(input.updates == 0, "activity overwrote main input frame");
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
    int finishedCalls = server.calls;
    input.update();  // The next main pass samples the physical release.
    activity.loop();
    require(activity.exits == 1, "first release after HTTP handler did not exit");
    require(server.calls == finishedCalls, "another request ran after the exit event");
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
    int priorCalls = server.calls;
    nowMs += activity.WIFI_ABANDON_MS + 1;
    activity.loop();
    require(activity.exits == 1 && activity.state == WebServerActivityState::SHUTTING_DOWN,
            "sustained outage did not leave transfer");
    require(server.calls == priorCalls, "request processed after abandoning WiFi");
  } else if (name == "stopped-server-back") {
    server.running = false;
    input.released = true;
    activity.loop();
    require(activity.exits == 1 && server.calls == 0, "stopped server trapped Back");
    require(!activity.skipLoopDelay(), "stopped server kept main busy");
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
