#include <Arduino.h>
#include <BoardConfig.h>
#include <HalGPIO.h>
#include <FileTransferBackLatch.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono;
static const auto started = steady_clock::now();
unsigned long millis() { return duration_cast<milliseconds>(steady_clock::now() - started).count(); }
static std::atomic<int> rawKey{-1};
static std::atomic<int> samples{0};
static std::atomic<bool> slowSample{false}, insideSample{false};
static bool failNextStart = false;
static int created = 0;
BoardConfig::Board BoardConfig::ACTIVE;
struct FakeTask { std::thread thread; };
static std::vector<std::unique_ptr<FakeTask>> tasks;
struct TaskExit {};
void vTaskDelay(TickType_t ms) { std::this_thread::sleep_for(milliseconds(ms)); }
void vTaskDelete(TaskHandle_t) { throw TaskExit{}; }
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t) { return 1176; }
int xTaskCreate(void (*fn)(void*), const char*, uint32_t, void* context, unsigned, TaskHandle_t* handle) {
  if (failNextStart) { failNextStart = false; return 0; }
  auto task = std::make_unique<FakeTask>();
  *handle = task.get();
  task->thread = std::thread([=] { try { fn(context); } catch (const TaskExit&) {} });
  tasks.push_back(std::move(task));
  ++created;
  return pdPASS;
}
void joinTasks() { for (auto& task : tasks) if (task->thread.joinable()) task->thread.join(); }
void HalGPIO::sampleButtonAdc(InputManager::ButtonAdcSample& first, InputManager::ButtonAdcSample& second) {
  insideSample = true;
  if (slowSample) vTaskDelay(30);
  const int key = rawKey.load();
  // Keys 0-3 sit on the first ladder, 4 and 5 (the side keys) on the second.
  const bool front = key >= 0 && key < 4;
  first = {1, !front ? 4095 : key == 0 ? 3610 : key == 1 ? 2500 : key == 2 ? 1200 : 0, front ? key : -1};
  second = {2, key == 4 ? 1500 : key == 5 ? 500 : 4095, key == 4 || key == 5 ? key : -1};
  if (key == -2) { first = {1, 0, 3}; second = {2, 0, 5}; }
  ++samples;
  insideSample = false;
}
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void settle() { vTaskDelay(45); }
void tap(int key, unsigned duration = 45) { rawKey = key; vTaskDelay(duration); rawKey = -1; settle(); }

// The full production activity loop is compiled below. Physical input is
// sampled through the SDK's current debounce and edge functions, extracted by
// run.py. The independent sampler uses the same physical schedule.
struct PhysicalInput {
  static constexpr uint8_t BTN_BACK = 0, BTN_POWER = 6;
  static constexpr unsigned long DEBOUNCE_DELAY = 5;
  uint8_t pressedEvents = 0, releasedEvents = 0, currentState = 0, lastState = 0;
  unsigned long lastDebounceTime = 0, buttonPressStart = 0, buttonPressFinish = 0;
  unsigned long powerButtonPressStart = 0, powerButtonPressFinish = 0;
  bool touchPressedEvent = false, touchReleasedEvent = false, touchLongPressEvent = false;
  bool multiTouchSwipeEvent = false, multiTouchRotationEvent = false, multiTouchPinchEvent = false;
  bool touchHomeKeyEvent = false;
  bool touchHomeKeyTapEvent = false, touchHomeKeyLongEvent = false;
  uint8_t getState() const { return rawKey == -2 ? (1u << 3) : rawKey >= 0 ? (1u << rawKey) : 0; }
  void updateConfirmBackHold(unsigned long) {}
  void updateConfirmPowerHold(unsigned long) {}
  void updateDigitalTwoButton(unsigned long) {}
  void applyStateChange(uint8_t, unsigned long);
  void update();
};
#include "production-input.inc"
struct MappedInputManager {
  enum class Button { Back };
  PhysicalInput physical;
  uint8_t backKey = 0;
  void update() { physical.update(); }
  uint8_t physicalBack() const { return backKey; }
  bool wasReleased(Button) const { return (physical.releasedEvents & (1u << backKey)) != 0; }
  bool wasHomeGesture() const { return false; }
};
#define LOG_DBG(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define LOG_ERR(...) ((void)0)
void yield() {}
void resetTaskWatchdogIfSubscribed() {}
enum wl_status_t { WL_DISCONNECTED, WL_CONNECTED };
struct { wl_status_t status() const { return WL_CONNECTED; } int RSSI() const { return -60; } } WiFi;
int barsForRssi(int, int) { return 3; }
struct FakeDns { void processNextRequest() {} } dns;
FakeDns* dnsServer = &dns;
static int dnsStops = 0;
void stopDnsServer() {
  if (!dnsServer) return;
  ++dnsStops;
  dnsServer = nullptr;
}
static int fakeServerCalls = 0;
static int fakeServerStops = 0;
static bool fakeServerCompleted = false;
static int fakeServerCancels = 0;
static int fakeServerStallEnds = 0;
static bool applierPresentAtBegin = false;
struct FakeServer {
  bool inHandler = false;
  bool running = true;
  unsigned handlerMs = 270;
  int calls = 0;
  bool completed = false;
  // The sender stopped mid-upload with the connection open.
  bool stalled = false;
  std::atomic<bool> interrupted{false};
  std::function<bool(uint8_t)> uiTextSizeApplier;
  std::function<bool()> uploadCancel;
  bool isRunning() const { return running; }
  void setUiTextSizeApplier(std::function<bool(uint8_t)> applier) { uiTextSizeApplier = std::move(applier); }
  void setUploadCancel(std::function<bool()> cancel) { uploadCancel = std::move(cancel); }
  void interruptUpload() { interrupted = true; }
  void begin() {
    applierPresentAtBegin = static_cast<bool>(uiTextSizeApplier);
    running = true;
  }
  void handleClient() {
    ++calls;
    ++fakeServerCalls;
    inHandler = true;
    if (stalled) {
      // Like WebServer waiting for the next upload byte: it polls the client every 2 ms and
      // gives up after 5 s. No chunk arrives, so the per-chunk cancel is never asked; an
      // interrupted socket reads as a dropped client at the next poll.
      const auto begin = millis();
      while (millis() - begin < 5000 && !interrupted) vTaskDelay(2);
      ++fakeServerStallEnds;
      inHandler = false;
      return;
    }
    // An upload arriving in 10 ms chunks; like the real server, each chunk asks the owner
    // whether to drop it, and a drop ends the request early.
    for (unsigned ms = 0; ms < handlerMs; ms += 10) {
      if (uploadCancel && uploadCancel()) {
        ++fakeServerCancels;
        inHandler = false;
        return;
      }
      vTaskDelay(handlerMs - ms < 10 ? handlerMs - ms : 10);
    }
    completed = true;
    fakeServerCompleted = true;
    inHandler = false;
  }
  bool sessionIdleExpired(unsigned long) const { return false; }
  void stop() {
    require(!inHandler, "server stopped inside an unfinished upload handler");
    running = false;
    ++fakeServerStops;
  }
};
using CrossPointWebServer = FakeServer;
template<class T> std::unique_ptr<T> makeUniqueNoThrow() { return std::make_unique<T>(); }
struct FakeFontCache { void releaseSdFontCaches() {} };
struct FakeRenderer { FakeFontCache* getFontCacheManager() { return nullptr; } };
static int renderLockAcquisitions = 0;
struct RenderLock {
  template<class T> explicit RenderLock(T&) { ++renderLockAcquisitions; }
};
static bool applyUiFontSizeResult = true;
static int applyUiFontSizeCalls = 0;
static uint8_t appliedUiTextSize = 255;
bool applyUiFontSize(FakeRenderer&, const uint8_t size) {
  ++applyUiFontSizeCalls;
  appliedUiTextSize = size;
  return applyUiFontSizeResult;
}
struct UITheme {
  int reloads = 0;
  static UITheme& getInstance() {
    static UITheme instance;
    return instance;
  }
  void reload() { ++reloads; }
};
struct { uint8_t frontButtonBack = 0, uiTextSize = 0; } SETTINGS;
HalGPIO gpio;
// The book return compiled with startWebServer; these sessions open outside a book.
struct { bool exists(const char*) const { return false; } } Storage;
struct ReaderActivity {
  static std::unique_ptr<ReaderActivity> create(FakeRenderer&, MappedInputManager&, const std::string&, bool) {
    return nullptr;
  }
};
struct { void replaceActivity(std::unique_ptr<ReaderActivity>&&) { require(false, "session outside a book reopened one"); } } activityManager;
enum class WebServerActivityState { SERVER_RUNNING, SHUTTING_DOWN, AP_STARTING };
struct CrossPointWebServerActivity {
  WebServerActivityState state = WebServerActivityState::SERVER_RUNNING;
  bool isApMode = true;
  std::unique_ptr<FakeServer> webServer = std::make_unique<FakeServer>();
  FileTransferBackLatch backLatch;
  FakeRenderer renderer;
  MappedInputManager mappedInput;
  unsigned long lastHandleClientTime = 0, firstDisconnectAt = 0;
  static constexpr unsigned long WIFI_ABANDON_MS = 300000;
  int consecutiveDisconnects = 0, lastWifiBars = 3, exits = 0, updates = 0;
  std::string returnBook;
  bool toBook = false;
  void requestUpdate() { ++updates; }
  void onGoHome() {
    require(!webServer || !webServer->inHandler, "activity exited during an unfinished upload handler");
    ++exits;
  }
  void loop();
  void startWebServer();
  void leave();
  void stopServerAndLeave();
#include "production-delay.inc"
};
#include "production-loop.inc"
#include "production-start.inc"

void run(const std::string& name) {
  HalGPIO gpio;
  if (name == "physical-pulse-in-handler") {
    CrossPointWebServerActivity activity;
    // The production start: it wires the upload cancel to the latch and starts the sampler.
    activity.state = WebServerActivityState::AP_STARTING;
    activity.startWebServer();
    require(activity.backLatch.active(), "sampler start failed");
    settle();
    activity.mappedInput.update();
    std::thread pulse([] { vTaskDelay(35); tap(0, 80); });
    const auto before = millis();
    activity.loop();
    pulse.join();
    // A main-only sampler has never observed the entirely enclosed pulse.
    activity.mappedInput.update();
    if (activity.webServer) activity.webServer->handlerMs = 0;
    if (!activity.exits) activity.loop();
    std::cout << "handler_ms=" << millis() - before << " main_press="
              << activity.mappedInput.physical.buttonPressStart << " main_release="
              << activity.mappedInput.physical.buttonPressFinish << " exits=" << activity.exits << '\n';
    require(activity.exits == 1, "physical Back entirely inside handler was lost");
    // Back inside a running upload drops it rather than waiting for it to finish.
    require(!fakeServerCompleted && fakeServerCancels == 1, "Back waited for the upload to finish");
    require(millis() - before < 270, "the upload held the loop past the Back tap");
    require(fakeServerCalls == 1, "another HTTP request ran before pending exit");
    require(!activity.webServer && fakeServerStops == 1 && dnsStops == 1,
            "Back exit skipped synchronous server or DNS cleanup");
    require(activity.mappedInput.physical.buttonPressStart == 0, "fixture sampled pulse in main");
    activity.backLatch.stop();
    require(activity.backLatch.stackFreeBytes() == 1176, "stack watermark not retained at stop");
    return;
  }
  if (name == "stalled-upload-back") {
    CrossPointWebServerActivity activity;
    activity.state = WebServerActivityState::AP_STARTING;
    activity.startWebServer();
    require(activity.backLatch.active(), "sampler start failed");
    activity.webServer->stalled = true;
    settle();
    activity.mappedInput.update();
    std::thread pulse([] { vTaskDelay(35); tap(0, 80); });
    const auto before = millis();
    activity.loop();
    const auto held = millis() - before;
    pulse.join();
    if (!activity.exits && activity.webServer) {
      activity.webServer->stalled = false;
      activity.webServer->handlerMs = 0;
      activity.loop();
    }
    std::cout << "stalled_handler_ms=" << held << " exits=" << activity.exits << '\n';
    require(fakeServerStallEnds == 1, "stalled upload fixture did not run");
    require(held < 600, "a stalled upload held Back until the library read timeout");
    require(activity.exits == 1 && fakeServerCalls == 1, "Back during a stalled upload did not leave at once");
    return;
  }
  if (name == "back-on-side-key-in-handler") {
    // Back assigned to a physical key past the four front keys, on the second ladder.
    SETTINGS.frontButtonBack = 4;
    CrossPointWebServerActivity activity;
    activity.mappedInput.backKey = 4;
    activity.state = WebServerActivityState::AP_STARTING;
    activity.startWebServer();
    require(activity.backLatch.active(), "Back on physical key 4 has no sampler");
    settle();
    activity.mappedInput.update();
    std::thread pulse([] { vTaskDelay(35); tap(4, 80); });
    const auto before = millis();
    activity.loop();
    pulse.join();
    if (!activity.exits && activity.webServer) {
      activity.webServer->handlerMs = 0;
      activity.loop();
    }
    std::cout << "side_key_handler_ms=" << millis() - before << " exits=" << activity.exits << '\n';
    require(activity.exits == 1, "Back on physical key 4 inside an upload was lost");
    require(!fakeServerCompleted && fakeServerCancels == 1, "Back on physical key 4 waited for the upload");
    return;
  }
  if (name == "oom-activity") {
    CrossPointWebServerActivity activity;
    activity.state = WebServerActivityState::AP_STARTING;
    failNextStart = true;
    activity.startWebServer();
    require(activity.exits == 1 && !activity.backLatch.active(), "sampler OOM did not request safe exit");
    activity.loop();
    require(activity.webServer->calls == 0, "HTTP handler ran after sampler OOM");
    return;
  }
  if (name == "ui-size-callback") {
    CrossPointWebServerActivity activity;
    activity.state = WebServerActivityState::AP_STARTING;
    activity.startWebServer();
    require(applierPresentAtBegin, "UI size applier was not installed before begin");
    require(activity.webServer && activity.webServer->uiTextSizeApplier, "UI size applier was not retained");
    const int updatesBefore = activity.updates;
    require(activity.webServer->uiTextSizeApplier(2), "valid UI size apply failed");
    require(renderLockAcquisitions == 1 && applyUiFontSizeCalls == 1 && appliedUiTextSize == 2,
            "UI size apply skipped render lock or renderer update");
    require(SETTINGS.uiTextSize == 2 && UITheme::getInstance().reloads == 1 && activity.updates == updatesBefore + 1,
            "successful UI size apply did not publish layout state");
    applyUiFontSizeResult = false;
    require(!activity.webServer->uiTextSizeApplier(1), "failed UI size apply was accepted");
    require(SETTINGS.uiTextSize == 2 && UITheme::getInstance().reloads == 1 && activity.updates == updatesBefore + 1,
            "failed UI size apply changed published state");
    activity.backLatch.stop();
    return;
  }
  if (name == "zero-chatter-activity") {
    CrossPointWebServerActivity activity;
    activity.mappedInput.backKey = 3;
    activity.webServer->handlerMs = 0;
    require(activity.backLatch.start(gpio, 3), "sampler start failed");
    settle();
    auto frames = [&](unsigned duration) {
      const auto end = millis() + duration;
      while (millis() < end && activity.exits == 0) {
        activity.mappedInput.update();
        activity.loop();
        vTaskDelay(1);
      }
    };
    // Existing main input accepts this 8 ms key 3 transient with its 5 ms
    // debounce. The active transfer sampler must own physical Back decisions.
    rawKey = -2; frames(8); rawKey = -1; frames(40);
    require(activity.mappedInput.physical.buttonPressStart != 0, "fixture did not exercise main debounce");
    require(activity.exits == 0, "main release bypassed sampler invalid-pair filter");
    rawKey = 3; frames(45); rawKey = -1; frames(45);
    require(activity.exits == 1, "valid remapped Back did not exit");
    return;
  }
  if (name == "destruction-joins") {
    auto latch = std::make_unique<FileTransferBackLatch>();
    require(latch->start(gpio, 0), "start failed");
    settle();
    slowSample = true;
    while (!insideSample) vTaskDelay(1);
    latch.reset();
    require(!insideSample, "destructor returned while ADC read still used context");
    int stoppedAt = samples;
    settle();
    require(samples == stoppedAt, "sampling continued after destruction");
    return;
  }
  FileTransferBackLatch latch;
  if (name == "oom-start") {
    failNextStart = true;
    require(!latch.start(gpio, 0), "task allocation failure was hidden");
    require(!latch.active() && !latch.consume(), "failed start retained active/pending state");
    latch.stop();
    require(latch.start(gpio, 0), "retry after OOM failed");
    settle(); tap(0);
    require(latch.consume(), "retry did not capture input");
    return;
  }
  if (name == "unsupported-board" || name == "mapping-out-of-range") {
    if (name == "unsupported-board") BoardConfig::ACTIVE.inputStyle = BoardConfig::InputStyle::DigitalButtons;
    // Power (6) is on neither ladder; its Back stays with main input.
    require(latch.start(gpio, name == "mapping-out-of-range" ? 6 : 0), "fallback start reported OOM");
    require(!latch.active() && created == 0 && !latch.consume(), "unsupported input started sampler");
    return;
  }
  if (name == "held-on-entry") rawKey = 0;
  const uint8_t back = name == "zero-chatter-mapping3" ? 3 : name == "second-ladder-back" ? 5 : 0;
  require(latch.start(gpio, back), "start failed");
  settle();
  if (name == "idle-pulse") {
    tap(0); require(latch.consume(), "idle Back tap lost");
    require(!latch.consume(), "pending exit consumed twice");
    tap(0); require(!latch.consume(), "same session fired twice");
  } else if (name == "held-on-entry") {
    rawKey = -1; settle();
    require(!latch.consume(), "key held through entry triggered exit");
    tap(0); require(latch.consume(), "fresh tap after release did not arm");
  } else if (name == "reentry-clears-old") {
    tap(0); latch.stop();
    require(!latch.consume(), "stopped generation leaked pending intent");
    require(latch.start(gpio, 0), "reentry failed");
    settle(); require(!latch.consume(), "prior generation exited reentry");
    tap(0); require(latch.consume(), "new generation did not capture Back");
  } else if (name == "unmapped") {
    tap(1); tap(2); tap(3);
    require(!latch.consume(), "unmapped physical key exited transfer");
    tap(0); require(latch.consume(), "configured Back lost");
  } else if (name == "second-ladder-back") {
    require(latch.active(), "Back on physical key 5 started no sampler");
    tap(0); tap(3); tap(4);
    require(!latch.consume(), "a key other than the assigned Back exited transfer");
    tap(5); require(latch.consume(), "Back on physical key 5 was lost");
  } else if (name == "zero-chatter-mapping3") {
    for (int i = 0; i < 3; ++i) { tap(-2, 8); tap(3, 8); }
    require(!latch.consume(), "short zero transient became remapped Back");
    // Even a long invalid pair must not become a stable key 3.
    tap(-2, 45); require(!latch.consume(), "paired-zero conversion accepted as Back");
    tap(3); require(latch.consume(), "valid remapped key 3 with other ladder idle was lost");
  } else {
    throw std::runtime_error("unknown case");
  }
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  int result = 0;
  try { run(argv[1]); std::cout << "PASS " << argv[1] << '\n'; }
  catch (const std::exception& e) { std::cerr << "FAIL " << argv[1] << ": " << e.what() << '\n'; result = 1; }
  joinTasks();
  return result;
}
