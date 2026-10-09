from pathlib import Path
import os
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src/activities/ActivityManager.cpp").read_text()
HEADER = (ROOT / "src/activities/ActivityManager.h").read_text()
PENDING_ACTION = re.search(r"enum class PendingAction \{[^}]+\};", HEADER).group()


def method(name):
    start = SOURCE.index("void ActivityManager::" + name + "(")
    end = re.search(r"\nvoid ActivityManager::", SOURCE[start + 1:])
    return SOURCE[start:start + 1 + end.start()]


STUBS = r'''
#include <atomic>
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
struct RenderLock { void unlock() {} };
using ActivityResult = int;
struct Input {
  bool suppressed = false;
  bool consumeSuppressedRelease() { bool value = suppressed; suppressed = false; return value; }
  bool wasHomeGesture() { return false; }
  bool hasTouch() { return false; }
  bool wasScreenTapped(int&, int&) { return false; }
  bool wasLightPanelGesture() { return false; }
  void resetHomeButtonInput() {}
};
struct Activity {
  std::string name;
  int result = 0;
  std::function<void(int)> resultHandler;
  bool exclusive = false;
  static inline int enters = 0;
  static inline int exits = 0;
  explicit Activity(std::string value) : name(std::move(value)) {}
  virtual ~Activity() = default;
  bool requiresExclusiveStorageLoop() { return exclusive; }
  bool isHomeActivity() { return name == "Home"; }
  bool handleHomeGesture() { return false; }
  void loop() {}
  void onEnter() { ++enters; }
  void onExit() { ++exits; }
};
struct SleepActivity : Activity { SleepActivity(int&, Input&, bool) : Activity("Sleep") {} };
struct FrontlightPanelActivity : Activity { FrontlightPanelActivity(int&, Input&) : Activity("FrontlightPanel") {} };
struct HeaderBackTapTarget { static bool contains(int, int) { return false; } static void clear() {} };
namespace haptic_feedback { void touchAction() {} }
namespace bleturner {
  bool ready = false;
  int calls = 0;
  bool beforeScreenChange() { ++calls; return ready; }
}
constexpr int eIncrement = 0;
void xTaskNotify(int, int, int) {}
struct ActivityManager {
  int renderer = 0;
  Input mappedInput;
  std::vector<std::unique_ptr<Activity>> stackActivities;
  std::unique_ptr<Activity> currentActivity = std::make_unique<Activity>("Reader");
  std::unique_ptr<Activity> pendingActivity;
  PENDING_ACTION_DECLARATION
  PendingAction pendingAction = PendingAction::None;
  unsigned screenVisit = 0;
  int renderTaskHandle = 0;
  std::atomic<bool> requestedUpdate = false;
  void loop();
  void exitActivity(const RenderLock&) {
    if (currentActivity) { currentActivity->onExit(); currentActivity.reset(); }
    HeaderBackTapTarget::clear();
  }
  void replaceActivity(std::unique_ptr<Activity>&&);
  void goToSleep(bool);
  void goHome() { replaceActivity(std::make_unique<Activity>("Home")); }
  void pushActivity(std::unique_ptr<Activity>&& next) {
    pendingActivity = std::move(next);
    pendingAction = PendingAction::Push;
  }
  void requestUpdate() { requestedUpdate = true; }
};
'''

CHECK = r'''
int main() {
  for (bool fromTimeout : {false, true}) {
    ActivityManager manager;
    manager.stackActivities.push_back(std::make_unique<Activity>("Home"));
    const int calls = bleturner::calls;
    const int enters = Activity::enters;
    const int exits = Activity::exits;
    manager.goToSleep(fromTimeout);
    assert(manager.currentActivity->name == "Sleep");
    assert(manager.pendingAction == ActivityManager::PendingAction::None);
    assert(manager.stackActivities.empty());
    assert(Activity::enters == enters + 1 && Activity::exits == exits + 2);
    assert(bleturner::calls == calls);
  }
  ActivityManager normal;
  normal.replaceActivity(std::make_unique<Activity>("Settings"));
  normal.loop();
  assert(normal.currentActivity->name == "Reader");
  assert(normal.pendingAction == ActivityManager::PendingAction::Replace);
  bleturner::ready = true;
  normal.loop();
  assert(normal.currentActivity->name == "Settings");
  bleturner::ready = false;
  ActivityManager namedSleep;
  namedSleep.replaceActivity(std::make_unique<Activity>("Sleep"));
  namedSleep.loop();
  assert(namedSleep.currentActivity->name == "Reader");
  ActivityManager empty;
  empty.currentActivity.reset();
  empty.goToSleep(false);
  assert(empty.currentActivity->name == "Sleep");
  assert(empty.pendingAction == ActivityManager::PendingAction::None);
  ActivityManager exclusive;
  exclusive.currentActivity->exclusive = true;
  exclusive.goToSleep(true);
  assert(exclusive.currentActivity->name == "Reader");
  ActivityManager suppressed;
  suppressed.mappedInput.suppressed = true;
  suppressed.goToSleep(true);
  assert(suppressed.currentActivity->name == "Reader");
}
'''


with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / "sleep.cpp"
    executable = Path(directory) / "sleep"
    source.write_text(STUBS.replace("PENDING_ACTION_DECLARATION", PENDING_ACTION) +
                      "\n".join(method(name) for name in ("loop", "replaceActivity", "goToSleep")) + CHECK)
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-Wall", "-Wextra", "-Werror",
                    str(source), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
    print("GREEN: timeout/manual sleep, normal transition guard, exclusive storage and suppressed release")
