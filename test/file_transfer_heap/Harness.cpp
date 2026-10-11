// The settings catalog (16,072 B on the X3, a static vector a settings screen or a pinned setting builds and keeps)
// must be returned before File transfer starts the radio: X3, 11/10/2026, tenor/ugly after a settings row, the server
// came up with 8,092 B free (largest 7,412) and a 575 KB upload stalled at 1,528 B free.
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <variant>

namespace {
bool locked = false;
bool catalogHeld = true;
bool releasedUnlocked = false;
const char* radioStart = nullptr;  // the first step that brings the radio up, and what it found
}  // namespace

void releaseBaseSettingsList() {
  if (!locked) releasedUnlocked = true;
  catalogHeld = false;
}
namespace filetransfer {
bool acquire() { return true; }
}  // namespace filetransfer
struct EspStub {
  unsigned getFreeHeap() const { return 0; }
  unsigned getMaxAllocHeap() const { return 0; }
};
[[maybe_unused]] EspStub ESP;
#define LOG_ERR(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define LOG_DBG(...) ((void)0)

struct FontCacheManager {
  void releaseSdFontCaches() {}
};
struct GfxRenderer {
  FontCacheManager fonts;
  FontCacheManager* getFontCacheManager() { return &fonts; }
};
struct MappedInputManager {};
struct Activity {
  void onEnter() {}
};
struct RenderLock {
  template <typename T>
  explicit RenderLock(T&) { locked = true; }
  ~RenderLock() { locked = false; }
};
enum class WebServerActivityState { MODE_SELECTION };
enum class NetworkMode { JOIN_NETWORK };
struct NetworkModeResult {
  NetworkMode mode;
};
struct ActivityResult {
  bool isCancelled = false;
  std::variant<std::monostate, NetworkModeResult> data;
};
struct NetworkModeSelectionActivity {
  NetworkModeSelectionActivity(GfxRenderer&, MappedInputManager&) {}
};

void noteRadio(const char* step) {
  if (!radioStart) radioStart = catalogHeld ? step : "clean";
}

class CrossPointWebServerActivity : public Activity {
 public:
  void onEnter();
  bool runtimeStarted = false;
  bool autoJoinForTest = true;
  GfxRenderer renderer;
  MappedInputManager mappedInput;
  WebServerActivityState state{};
  NetworkMode networkMode{};
  bool isApMode = true;
  std::string connectedIP, connectedSSID;
  unsigned long lastHandleClientTime = 1;
  void leave() {}
  void requestUpdate() {}
  // The chooser (and the Wi-Fi it leads to) or the test's direct join: the radio comes after either.
  void onNetworkModeSelected(NetworkMode) { noteRadio("join with the catalog held"); }
  template <typename A, typename F>
  void startActivityForResult(std::unique_ptr<A>, F&&) { noteRadio("chooser with the catalog held"); }
};

#include "production-onenter.inc"

int main() {
  CrossPointWebServerActivity activity;
  activity.onEnter();
  const bool ok = radioStart && std::string(radioStart) == "clean" && !releasedUnlocked;
  std::printf("%s (radio step: %s%s)\n", ok ? "PASS" : "FAIL", radioStart ? radioStart : "none",
              releasedUnlocked ? ", released without the render lock" : "");
  return ok ? 0 : 1;
}
