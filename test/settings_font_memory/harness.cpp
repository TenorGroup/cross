#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <vector>
#include <BoardConfig.h>
#include <HalClock.h>
#include <HalTiltSensor.h>
#include <SdCardFontRegistry.h>
#include <FreeInkUI.h>
#include <ObfuscationUtils.h>
#include "SettingsList.h"
#include "activities/util/KeyboardLayoutSet.h"

namespace mem {
struct alignas(std::max_align_t) Header { size_t bytes; unsigned epoch; };
unsigned epoch = 0;
bool active = false;
size_t live = 0, peak = 0;
bool requireReleaseLock = false;
unsigned renderLockDepth = 0;
}
void* operator new(size_t n) {
  auto* h = static_cast<mem::Header*>(std::malloc(sizeof(mem::Header) + (n ? n : 1)));
  if (!h) throw std::bad_alloc();
  h->bytes = n; h->epoch = mem::active ? mem::epoch : 0;
  if (mem::active) { mem::live += n; mem::peak = std::max(mem::peak, mem::live); }
  return h + 1;
}
void operator delete(void* p) noexcept {
  if (!p) return;
  auto* h = static_cast<mem::Header*>(p) - 1;
  if (h->epoch && h->epoch == mem::epoch) {
    if (mem::requireReleaseLock && mem::renderLockDepth == 0) std::abort();
    mem::live -= h->bytes;
  }
  std::free(h);
}
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, size_t) noexcept { ::operator delete(p); }
#include "Boundaries.inc"
namespace fui = freeink::ui;
namespace shell {
bool isUgly() { return false; }
}
struct ActivityResult { bool isCancelled = false; };
struct FontDownloadActivity {
  template<class A, class B> FontDownloadActivity(A&, B&) {}
  FontDownloadActivity() = default;
  enum State { STORE_LIST, LOADING_MANIFEST, ERROR, GROUP_LIST, FAMILY_LIST } state_ = FAMILY_LIST;
  struct Nav { std::atomic<int> selected{0}; void reset() {} } groupNav_, nav;
  // The store list and the update-one-font path of onWifiSelectionComplete.
  std::string updateTarget_, errorMessage_;
  bool networkUsed_ = false, errorOnly_ = false;
  std::vector<int> filteredIndices_;
  int specialRowCount() const { return 0; }
  int downloadingFamilyIndex_ = 0;
  size_t currentFileIndex_ = 0, currentFileTotal_ = 0;
  struct Family { uint32_t fileCount = 0; bool hasUpdate = false; int name = 0; };
  std::vector<Family> families_;
  const char* str(int) const { return ""; }
  void loadInstalledList() {}
  int storeRowCount() const { return 2; }
  void downloadFamily(Family&) {}
  void requestUpdate() {}
  struct Renderer {
    Renderer* getFontCacheManager() { return this; }
    void releaseSdFontCaches() {}
  } renderer;
  bool rowsDirty_ = false, finished = false, locked = false, sawReleased = false, fetchOk = true;
  void finish() { finished = true; }
  void requestUpdateAndWait() {}
  bool fetchAndParseManifest() { sawReleased = mem::live == 0; return fetchOk; }
  void clearManifest() {}
  bool hasGroupScreen() const { return false; }
  void buildFilteredIndices(int) {}
  void onWifiSelectionComplete(bool);
};
struct { void mode(int) {} } WiFi;
constexpr int WIFI_OFF = 0;
struct RenderLock {
  FontDownloadActivity* owner = nullptr;
  explicit RenderLock(FontDownloadActivity& value) : owner(&value) {
    if(owner->locked) std::abort();
    owner->locked = true; ++mem::renderLockDepth;
  }
  template<class T> explicit RenderLock(T&) { ++mem::renderLockDepth; }
  ~RenderLock() { if (owner) owner->locked = false; --mem::renderLockDepth; }
};
namespace obfuscation {
String obfuscateToBase64(const std::string& s) { return String(s.c_str()); }
std::string deobfuscateFromBase64(const char* s, size_t, bool* ok, bool* tooLong) {
  if (ok) *ok = true;
  if (tooLong) *tooLong = false;
  return s ? s : "";
}
}

#include "PostWifiMethods.inc"

bool check(bool value, const char* message) {
  if (!value) std::printf("FAIL %s\n", message);
  return value;
}

std::string jsonSettings() {
  JsonDocument doc;
  SETTINGS.toJson(doc);
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool catalogReleaseProbe() {
  ++mem::epoch; mem::live = mem::peak = 0; mem::active = true;
  const auto& initial = getBaseSettingsList();
  mem::active = false;
  const size_t bytes = mem::live;
  const size_t sourceRows = initial.size();
  const auto copied = initial;
  const auto webCopy = getSettingsList();
  const auto clock = std::find_if(copied.begin(), copied.end(), [](const auto& row) {
    return row.valuePtr == &CrossPointSettings::statusBarClock;
  });
  const auto clockCopy = buildTenorClockPlacementSetting(*clock);
  const std::string beforeJson = jsonSettings();
  releaseBaseSettingsList();
  bool ok = check(mem::live == 0, "shared catalog allocations released");
  std::printf("catalog_rows=%zu sizeof_SettingInfo=%zu allocated=%zu after_release=%zu\n",
              copied.size(), sizeof(SettingInfo), bytes, mem::live);
  ok &= check(sourceRows > 0 && copied.size() == sourceRows && !webCopy.empty(), "base rows and owned web copy survive release");
  clockCopy.valueSetter(1);
  ok &= check(clockCopy.valueGetter() == 1, "copied dynamic callback survives catalog release");
  SETTINGS.statusBarClock = CrossPointSettings::STATUS_BAR_CLOCK_HIDE;
  // Compare every field that shapes ordering, JSON persistence and enum rendering.
  mem::active = true;
  const auto& rebuilt = getBaseSettingsList();
  mem::active = false;
  ok &= check(rebuilt.size() == copied.size() && rebuilt.capacity() == copied.size(), "exact rebuild capacity/count");
  for (size_t i = 0; i < std::min(rebuilt.size(), copied.size()); ++i) {
    const auto& a = copied[i]; const auto& b = rebuilt[i];
    ok &= check(a.nameId == b.nameId && a.type == b.type && a.valuePtr == b.valuePtr &&
                a.enumValues == b.enumValues && a.enumStringValues == b.enumStringValues && a.action == b.action &&
                a.valueRange.min == b.valueRange.min && a.valueRange.max == b.valueRange.max && a.valueRange.step == b.valueRange.step &&
                a.key == b.key && a.category == b.category && a.obfuscated == b.obfuscated &&
                a.inTextSettings == b.inTextSettings && a.stringOffset == b.stringOffset && a.stringMaxLen == b.stringMaxLen,
                "descriptor order/keys/enums/persistence fields unchanged");
  }
  JsonDocument prior;
  deserializeJson(prior, beforeJson);
  ok &= check(SETTINGS.fromJson(prior.as<JsonVariantConst>()), "real settings JSON reload succeeds");
  ok &= check(jsonSettings() == beforeJson, "real settings JSON byte-equivalent roundtrip");
  // A Wi-Fi time save builds no catalog (forEachBaseSetting, v1.0.53). Release must still happen after that
  // child returns and before manifest TLS, using the actual production callback.
  releaseBaseSettingsList();
  JsonDocument wifiSave;
  mem::active = true;
  SETTINGS.toJson(wifiSave);
  mem::active = false;
  ok &= check(mem::live == 0, "Wi-Fi save builds no catalog");
  FontDownloadActivity font;
  mem::requireReleaseLock = true;
  font.onWifiSelectionComplete(true);
  mem::requireReleaseLock = false;
  ok &= check(font.sawReleased && !font.locked, "catalog released after Wi-Fi before manifest TLS");
  FontDownloadActivity cancelled;
  cancelled.onWifiSelectionComplete(false);
  // Backing out of Wi-Fi returns to the store list (no TLS ran, so nothing to restart from).
  ok &= check(!cancelled.finished && cancelled.state_ == FontDownloadActivity::STORE_LIST, "Wi-Fi cancellation returns to the store list");
  // Fail manifest and rebuild settings on return in the lifecycle test below.
  FontDownloadActivity failed;
  failed.fetchOk = false;
  failed.onWifiSelectionComplete(true);
  ok &= check(failed.state_ == FontDownloadActivity::ERROR && failed.errorOnly_ && failed.sawReleased, "manifest failure preserves release and error path");
  ok &= check(jsonSettings() == beforeJson, "persistence getter rebuilds safely after release/error");
  return ok;
}

int main() {
  BoardConfig::ACTIVE = BoardConfig::XTEINK_X3;
  gpio.x3 = true; halTiltSensor.available = true;
  (void)I18N.get(StrId::STR_SETTINGS_TITLE);
  bool ok = catalogReleaseProbe();
  std::printf("settings_font_memory=%s\n", ok ? "GREEN" : "RED");
  return ok ? 0 : 1;
}
