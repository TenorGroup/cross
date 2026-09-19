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
  first = {1, key == -1 ? 4095 : key == 0 ? 3610 : key == 1 ? 2500 : key == 2 ? 1200 : 0, key < 0 ? -1 : key};
  second = {2, 4095, -1};
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
  bool multiTouchSwipeEvent = false, multiTouchRotationEvent = false, touchHomeKeyEvent = false;
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
  uint8_t physicalBack = 0;
  void update() { physical.update(); }
  bool wasReleased(Button) const { return (physical.releasedEvents & (1u << physicalBack)) != 0; }
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
struct FakeServer {
  bool inHandler = false;
  bool running = true;
  unsigned handlerMs = 270;
  int calls = 0;
  bool completed = false;
  bool isRunning() const { return running; }
  void begin() { running = true; }
  void handleClient() {
    ++calls;
    inHandler = true;
    vTaskDelay(handlerMs);
    completed = true;
    inHandler = false;
  }
};
using CrossPointWebServer = FakeServer;
template<class T> std::unique_ptr<T> makeUniqueNoThrow() { return std::make_unique<T>(); }
struct FakeFontCache { void releaseSdFontCaches() {} };
struct FakeRenderer { FakeFontCache* getFontCacheManager() { return nullptr; } };
struct RenderLock { template<class T> explicit RenderLock(T&) {} };
struct { uint8_t frontButtonBack = 0; } SETTINGS;
HalGPIO gpio;
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
  int consecutiveDisconnects = 0, lastWifiBars = 3, exits = 0;
  void requestUpdate() {}
  void onGoHome() {
    require(!webServer->inHandler, "activity exited during an unfinished upload handler");
    ++exits;
  }
  void loop();
  void startWebServer();
#include "production-delay.inc"
};
#include "production-loop.inc"
#include "production-start.inc"

void run(const std::string& name) {
  HalGPIO gpio;
  if (name == "physical-pulse-in-handler") {
    CrossPointWebServerActivity activity;
    require(activity.backLatch.start(gpio, 0), "sampler start failed");
    settle();
    activity.mappedInput.update();
    std::thread pulse([] { vTaskDelay(35); tap(0, 80); });
    const auto before = millis();
    activity.loop();
    pulse.join();
    // A main-only sampler has never observed the entirely enclosed pulse.
    activity.mappedInput.update();
    activity.webServer->handlerMs = 0;
    if (!activity.exits) activity.loop();
    std::cout << "handler_ms=" << millis() - before << " main_press="
              << activity.mappedInput.physical.buttonPressStart << " main_release="
              << activity.mappedInput.physical.buttonPressFinish << " exits=" << activity.exits << '\n';
    require(activity.exits == 1, "physical Back entirely inside handler was lost");
    require(activity.webServer->completed, "upload handler was interrupted before completion");
    require(activity.webServer->calls == 1, "another HTTP request ran before pending exit");
    require(activity.mappedInput.physical.buttonPressStart == 0, "fixture sampled pulse in main");
    activity.backLatch.stop();
    require(activity.backLatch.stackFreeBytes() == 1176, "stack watermark not retained at stop");
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
  if (name == "zero-chatter-activity") {
    CrossPointWebServerActivity activity;
    activity.mappedInput.physicalBack = 3;
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
    require(latch.start(gpio, name == "mapping-out-of-range" ? 4 : 0), "fallback start reported OOM");
    require(!latch.active() && created == 0 && !latch.consume(), "unsupported input started sampler");
    return;
  }
  if (name == "held-on-entry") rawKey = 0;
  require(latch.start(gpio, name == "zero-chatter-mapping3" ? 3 : 0), "start failed");
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
