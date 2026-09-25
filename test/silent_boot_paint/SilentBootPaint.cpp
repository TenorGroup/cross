#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

// The production paint request, waited paint and render task, the flush at the end of
// ActivityManager::loop() and the tail of a silent boot in setup(), on fake FreeRTOS tasks.
struct FakeTask {
  std::mutex mutex;
  std::condition_variable cv;
  unsigned count = 0;
};
using TaskHandle_t = FakeTask*;
using SemaphoreHandle_t = void*;
thread_local FakeTask* self = nullptr;
enum eNotifyAction { eIncrement };
constexpr int pdTRUE = 1;
constexpr unsigned portMAX_DELAY = ~0u;
void xTaskNotify(TaskHandle_t task, unsigned, eNotifyAction) {
  std::lock_guard<std::mutex> lock(task->mutex);
  ++task->count;
  task->cv.notify_all();
}
unsigned ulTaskNotifyTake(int, unsigned) {
  std::unique_lock<std::mutex> lock(self->mutex);
  self->cv.wait(lock, [] { return self->count > 0; });
  const unsigned taken = self->count;
  self->count = 0;
  return taken;
}
TaskHandle_t xTaskGetCurrentTaskHandle() { return self; }
TaskHandle_t xSemaphoreGetMutexHolder(SemaphoreHandle_t) { return nullptr; }
static std::mutex critical;
[[maybe_unused]] static int activityManagerSpinlock = 0;
#define taskENTER_CRITICAL(spinlock) ((void)(spinlock), critical.lock())
#define taskEXIT_CRITICAL(spinlock) ((void)(spinlock), critical.unlock())
struct RenderLock {
  RenderLock() = default;
  RenderLock(RenderLock&&) = default;
};
struct HalPowerManager {
  struct Lock {
    Lock() {}
  };
};
struct {
  void setInverted(bool) {}
} display;
struct {
  int screenInverted = 0;
} SETTINGS;
std::atomic<bool> frameAfterDeferredWrite{false};

struct Activity {
  std::atomic<int> paints{0};
  void render(RenderLock&&) {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    ++paints;
  }
};

class ActivityManager {
 public:
  TaskHandle_t renderTaskHandle = nullptr;
  TaskHandle_t waitingTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  std::atomic<bool> requestedUpdate{false};
  bool sleepTransition = false;
  std::unique_ptr<Activity> currentActivity;
  [[noreturn]] void renderTaskLoop();
  void requestUpdate(bool immediate = false);
  void requestUpdateAndWait();
#if HAS_FIRST_PAINT
  void requestFirstPaintAndWait();
#endif
  void loopFlush();
};
#include "production-manager.inc"
void ActivityManager::loopFlush() {
#include "production-flush.inc"
}

ActivityManager activityManager;
struct {
  void update() {}
} gpio;
void delay(int) {}
enum class BootResume { Splash, Silent, SplashlessWake };
void silentBootTail(const BootResume resume) {
#include "production-silent.inc"
}

int main() {
  FakeTask mainTask, renderTask;
  self = &mainTask;
  activityManager.renderTaskHandle = &renderTask;
  std::thread([] {
    self = activityManager.renderTaskHandle;
    activityManager.renderTaskLoop();
  }).detach();
  activityManager.currentActivity = std::make_unique<Activity>();
  // setup() lands on Home outside loop(); Home's onEnter() queues its own paint.
  activityManager.requestUpdate();
  silentBootTail(BootResume::Silent);
  const int waited = activityManager.currentActivity->paints;
  activityManager.loopFlush();  // the first loop() pass
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  const int paints = activityManager.currentActivity->paints;
  std::cout << "paints_after_wait=" << waited << " paints=" << paints << std::endl;
  if (waited != 1) {
    std::cerr << "FAIL the silent boot did not wait for its first paint\n";
    std::_Exit(1);
  }
  if (paints != 1) {
    std::cerr << "FAIL Home painted " << paints << " times after a silent restart\n";
    std::_Exit(1);
  }
  std::cout << "PASS silent-boot-single-paint" << std::endl;
  std::_Exit(0);  // the render task never returns
}
