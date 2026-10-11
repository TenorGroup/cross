#include <memory>
#include <cstdio>
static bool locked = false, held = true, good = true;
void releaseBaseSettingsList() { good &= locked; held = false; }
namespace filetransfer { bool acquire() { return true; } }
#define LOG_DBG(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define LOG_ERR(...) ((void)0)
struct RenderLock {
  template<typename T> RenderLock(T&) { locked = true; }
  ~RenderLock() { locked = false; }
};
struct Activity { void onEnter() {} };
constexpr int WIFI_STA = 1;
struct { void mode(int) { good &= !held; } } WiFi;
struct ActivityResult { bool isCancelled = false; };
struct WifiSelectionActivity {
  bool automatic, clock, only, deferred;
  WifiSelectionActivity(int&, int&, bool a, bool c, bool o = false, bool d = false)
      : automatic(a), clock(c), only(o), deferred(d) {}
};
template<typename T, typename... A> auto makeUniqueNoThrow(A&&... a) {
  return std::make_unique<T>(std::forward<A>(a)...);
}
struct OtaUpdateActivity : Activity {
  bool runtimeStarted = false, idleTimerStarted = true;
  struct { bool armed = false; } boot;
  int renderer = 0, mappedInput = 0;
  void finish() { good = false; }
  void onWifiSelectionComplete(bool) {}
  void onEnter();
  template<typename F> void startActivityForResult(std::unique_ptr<WifiSelectionActivity> child, F) {
    good &= child && child->automatic && !child->clock && !child->only && child->deferred == boot.armed;
  }
};
#include "production-ota-enter.inc"
int main() {
  for (bool armed : {false, true}) {
    held = true;
    OtaUpdateActivity screen;
    screen.boot.armed = armed;
    screen.onEnter();
  }
  std::printf("%s OTA releases catalog under render lock before radio and preserves update-boot handoff\n",
              good ? "PASS" : "FAIL");
  return !good;
}
