#include <Arduino.h>
#include <BoardConfig.h>
#include <Logging.h>
#include <ReleaseJsonParser.h>
#include <esp_ota_ops.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

#include "HttpDownloader.h"
#include "UpdateBoot.h"
#include "OtaUpdater.h"
#include "TlsRecordSlot.h"

// The update screen's production steps (check, confirm, install, dry run, exit) and the
// production restart into the update boot, around the RTC slot setup() takes on every boot.
BoardConfig::Board BoardConfig::ACTIVE;
// Heap allocations while `counting` is set: a progress frame must make none.
static bool counting = false;
static int allocations = 0;
void* operator new(size_t size) {
  if (counting) ++allocations;
  if (void* p = std::malloc(size ? size : 1)) return p;
  throw std::bad_alloc();
}
void* operator new[](size_t size) { return operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
unsigned long millis() {
  static unsigned long now = 0;
  return now += 5;
}
void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

// Update slot and transport: the manifest, then a small C3 image of this board.
OtaCalls otaCalls;
static const esp_partition_t spare{1 << 20};
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) { return &spare; }
esp_err_t esp_ota_begin(const esp_partition_t*, size_t, esp_ota_handle_t* handle) {
  ++otaCalls.begin;
  *handle = 1;
  return ESP_OK;
}
esp_err_t esp_ota_write(esp_ota_handle_t, const void*, size_t len) {
  otaCalls.written += len;
  return ESP_OK;
}
esp_err_t esp_ota_abort(esp_ota_handle_t) {
  ++otaCalls.abort;
  return ESP_OK;
}
esp_err_t esp_ota_end(esp_ota_handle_t) {
  ++otaCalls.end;
  return ESP_OK;
}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t*) {
  ++otaCalls.setBoot;
  return ESP_OK;
}
namespace firmware_flash {
uint16_t runningPartitionChipId() { return 5; }
}  // namespace firmware_flash
static std::vector<uint8_t> image;
const char* fixtureTag = "v9.9.9";
size_t ReleaseJsonParser::getFirmwareSize() const { return image.size(); }
static bool imageDrops = false;
bool HttpDownloader::fetchUrl(const std::string&, const DataCallback&, const std::string&, const std::string&,
                              const char*, bool, ProgressCallback, bool*, TransferStats* stats) {
  if (stats) *stats = {200, true, 0, 0, 0, 0, 90000, 50000, 50000};
  return true;  // the manifest
}
// The image server: parts on request, a connection that breaks at breakAt (`breaks` times), or
// one that ignores Range and sends the whole body.
static struct {
  size_t breakAt = SIZE_MAX;
  int breaks = 0;
  bool ignoresRange = false;
  bool ignoresRangeAfterFirst = false;
  std::vector<size_t> firsts;
} server;
bool HttpDownloader::fetchRange(const std::string&, size_t first, size_t last, const DataCallback& onData,
                                const char*, ProgressCallback, bool*, TransferStats* stats, bool* whole) {
  if (stats) *stats = {206, true, 0, 0, 0, 0, 90000, 50000, 50000};
  server.firsts.push_back(first);
  const bool ignores = server.ignoresRange || (server.ignoresRangeAfterFirst && first > 0);
  *whole = ignores;
  if (ignores) {
    if (first != 0) return false;  // what the downloader does with a 200 after byte 0
    last = image.size() - 1;
  }
  const size_t end = std::min(imageDrops ? image.size() / 2 : image.size(), last + 1);
  for (size_t sent = first; sent < end;) {
    size_t n = std::min<size_t>(1024, end - sent);
    const bool breaksHere = server.breaks > 0 && sent <= server.breakAt && sent + n >= server.breakAt;
    if (breaksHere) n = server.breakAt - sent;
    if (n && !onData(image.data() + sent, n)) return false;
    sent += n;
    if (breaksHere) {
      --server.breaks;
      return false;
    }
  }
  return end == std::min(image.size(), last + 1);
}

// What the screen reaches outside itself.
struct FakeSerial {
  std::string text;
  void printf(const char* format, ...) {
    char line[512];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    text += line;
  }
  void print(const char* line) { text += line; }
} logSerial;
static std::string keptLine;
void probeKeepDryRunLine(const char* line) { keptLine = line; }
enum StrId {
  STR_CANCEL,
  STR_UPDATE,
  STR_NEW_UPDATE,
  STR_FIRMWARE_WRONG_DEVICE,
  STR_CHECKING_UPDATE,
  STR_CURRENT_VERSION,
  STR_NEW_VERSION,
  STR_UPDATING,
  STR_NO_UPDATE,
  STR_BACK,
  STR_UPDATE_FAILED,
  STR_UPDATE_COMPLETE,
  STR_POWER_ON_HINT
};
const char* tr(StrId) { return "text"; }
struct FakeFontCache {
  void releaseSdFontCaches() {}
  void releaseBuiltinPageCaches() {}
} fontCache;
// What a frame drew, kept without allocating: the counts and, per text, whether it was black.
struct FakeRenderer {
  int clears = 0, headers = 0, displays = 0, texts = 0;
  bool black[16] = {};
  char text[16][24] = {};
  FakeFontCache* getFontCacheManager() { return &fontCache; }
  int getScreenWidth() const { return 480; }
  int getScreenHeight() const { return 800; }
  int getLineHeight(int) const { return 20; }
  void clearScreen() {
    ++clears;
    texts = 0;
  }
  void drawText(int, int, int, const char* t, bool isBlack = true) { drawCenteredText(0, 0, t, isBlack); }
  void drawCenteredText(int, int, const char* t, bool isBlack = true, int = 0) {
    if (texts < 16) {
      black[texts] = isBlack;
      std::snprintf(text[texts], sizeof(text[texts]), "%s", t);
      ++texts;
    }
  }
  void displayBuffer() { ++displays; }
};
struct Rect {
  int x, y, width, height;
};
struct EpdFontFamily {
  enum Style { REGULAR, BOLD };
};
constexpr int UI_10_FONT_ID = 10;
struct Metrics {
  int topPadding = 0, headerHeight = 40, contentSidePadding = 10, verticalSpacing = 5, progressBarHeight = 12;
};
struct UITheme {
  enum class TextVerticalAlignment { TOP };
  static UITheme& getInstance() {
    static UITheme theme;
    return theme;
  }
  const Metrics& getMetrics() const { return metrics; }
  static void drawCenteredWrappedText(FakeRenderer&, Rect, int, const char*, int, bool, EpdFontFamily::Style,
                                      TextVerticalAlignment) {}
  Metrics metrics;
};
struct {
  void drawHeader(FakeRenderer& r, Rect, const char*) { ++r.headers; }
  void drawProgressBar(FakeRenderer&, Rect, int, int) {}
  void drawButtonHints(FakeRenderer&, const char*, const char*, const char*, const char*) {}
} fakeTheme;
#define GUI fakeTheme
struct {
  void releaseForOta(FakeRenderer&) {}
} sdFontSystem;
enum wifi_mode_t { WIFI_MODE_NULL, WIFI_MODE_STA };
struct {
  wifi_mode_t mode = WIFI_MODE_STA;
  wifi_mode_t getMode() const { return mode; }
  void disconnect(bool) {}
} WiFi;
namespace filetransfer {
inline void release() {}
}  // namespace filetransfer
struct Labels {
  const char *btn1, *btn2, *btn3, *btn4;
};
struct MappedInputManager {
  enum class Button { Back };
  Labels mapLabels(const char* a, const char* b, const char* c, const char* d) const { return {a, b, c, d}; }
  uint8_t physicalBack() const { return 0; }
  void suppressNextRelease(Button) const {}
};
struct HalGPIO {
} gpio;
struct FakeLatch {
  bool start(HalGPIO&, uint8_t) { return true; }
  void stop() {}
  bool latched() const { return false; }
};
struct OptionPopup {
  std::function<void(int)> chosen;
  int shown = 0;
  void show(const char*, const char* const*, int, int, std::function<void(int)> onChoice) {
    ++shown;
    chosen = std::move(onChoice);
  }
  void show(const char* title, const char*, const char* const* options, int count, int current,
            std::function<void(int)> onChoice) {
    show(title, options, count, current, std::move(onChoice));
  }
  bool processRender(FakeRenderer&, const MappedInputManager&) { return false; }
};

// The restarts, and the RTC slot they arm (main.cpp).
update_boot::Slot otaBootSlot;
static bool deepSleepInProgress = false;
constexpr uint32_t SILENT_REBOOT_TARGET_OTA = 3;
static std::vector<std::string> restarts;
static void silentRestartTo(uint32_t, const char* target) { restarts.push_back(target); }
void silentRestart() { restarts.push_back("home"); }
#include "production-restart.inc"

struct Activity {
  void onExit() {}
};
struct OtaUpdateActivity;
struct RenderLock {
  explicit RenderLock(OtaUpdateActivity&) {}
};
struct OtaUpdateActivity : Activity {
  enum State {
    WIFI_SELECTION,
    CHECKING_FOR_UPDATE,
    WAITING_CONFIRMATION,
    UPDATE_IN_PROGRESS,
    NO_UPDATE,
    FAILED,
    FINISHED,
    SHUTTING_DOWN
  };
  explicit OtaUpdateActivity(const update_boot::Request& bootRequest = {}) : boot(bootRequest) {}
  static constexpr unsigned int UNINITIALIZED_PERCENTAGE = 111;
  unsigned int lastUpdaterPercentage = UNINITIALIZED_PERCENTAGE;
  State state = WIFI_SELECTION;
  bool runtimeStarted = true;
  OtaUpdater updater;
  const char* failedDetail = nullptr;
  OptionPopup confirmPopup;
  FakeLatch backLatch;
  const update_boot::Request boot;
  bool restartIntoInstall = false;
  FakeRenderer renderer;
  MappedInputManager mappedInput;
  int finishes = 0;
  std::vector<std::string> recorded;
  void finish() { ++finishes; }
  // Frames drawn on request, as the render task would: the allocations each one made.
  bool paints = false;
  bool firstFrameWarmed = false;
  std::vector<int> frameAllocations;
  void paint() {
    if (!paints) return;
    if (state != UPDATE_IN_PROGRESS) return render(RenderLock(*this));
    const int before = allocations;
    counting = true;
    render(RenderLock(*this));
    counting = false;
    if (frameAllocations.empty()) {
      int warm = -1, bytes = -1;
      for (int i = 0; i < renderer.texts; ++i) {
        if (!renderer.black[i] && std::strcmp(renderer.text[i], "0123456789 /%") == 0) warm = i;
        if (renderer.black[i] && std::strncmp(renderer.text[i], "0 / ", 4) == 0) bytes = i;
      }
      firstFrameWarmed = warm >= 0 && bytes > warm;
    }
    frameAllocations.push_back(allocations - before);
  }
  void requestUpdate(bool = false) { paint(); }
  void requestUpdateAndWait() { paint(); }
  void render(RenderLock&&);
  void recordAttempt(const char* op, bool = false) { recorded.push_back(op); }
  void onWifiSelectionComplete(bool success);
  void runUpdateInstall();
  void onExit();
#ifdef TENOR_PRESS_PROBE
  static constexpr size_t PROBE_LINE_BYTES = 640;
  struct {
    uint32_t bootHeap, bootLargest, wifiHeap, wifiLargest, installHeap, installLargest;
  } probeStages = {};
  std::string dryRunUrl = "https://cross.tenor.vn/firmware/test/dry-run.json";
  uint8_t dryRunsAfter = 0;
  void runDryRun();
#endif
};
#include "production-screen.inc"

static update_boot::Request bootNow() { return update_boot::take(otaBootSlot); }

// ---- The flag: armed by name only, spent on the next boot whatever that boot does.
void flagCases(const std::string& name) {
  if (name == "normal-boot-is-not-an-update-boot") {
    update_boot::Slot slot{};
    require(!update_boot::take(slot).armed, "a zeroed slot started an update boot");
    for (const uint32_t fill : {0xFFFFFFFFu, 0xA5A5A5A5u, update_boot::MAGIC}) {
      slot = {fill, fill, fill, fill};
      require(!update_boot::take(slot).armed, "RTC garbage started an update boot");
    }
    return;
  }
  if (name == "armed-flag-is-spent-once") {
    update_boot::arm(otaBootSlot);
    const auto first = bootNow();
    require(first.armed && !first.dryRun(), "the armed install did not start an update boot");
    require(!bootNow().armed, "the next boot started another update: a crash or power cut would loop");
    return;
  }
  if (name == "power-loss-leaves-no-update-boot") {
    update_boot::arm(otaBootSlot, 3, 3);
    otaBootSlot.dryRunsLeft ^= 0x40;  // one flipped bit
    require(!bootNow().armed, "a damaged slot started an update boot");
    otaBootSlot = {update_boot::MAGIC, 0, 0, 0x12345678};  // the magic, and an install, by chance
    require(!bootNow().armed, "garbage that matched the magic started an install");
    update_boot::arm(otaBootSlot, 4, 3);
    require(!bootNow().armed, "more runs left than in the series");
    update_boot::arm(otaBootSlot, 30, 30);
    require(!bootNow().armed, "a series longer than the command allows");
    return;
  }
  throw std::runtime_error("unknown case");
}

// ---- The screen: the check alone arms nothing, Update arms the install, the update boot installs.
void screenCases(const std::string& name) {
  image.assign(64 * 1024, 0);
  image[0] = 0xE9;
  image[12] = 5;
  const char tag[] = "CROSSPOINT-BOARD-V1:x4;";
  std::memcpy(image.data() + 100, tag, sizeof(tag) - 1);
  if (name == "check-alone-arms-nothing" || name == "update-restarts-into-install" ||
      name == "cancel-restarts-home") {
    OtaUpdateActivity screen;
    screen.onWifiSelectionComplete(true);
    require(screen.state == OtaUpdateActivity::WAITING_CONFIRMATION && screen.confirmPopup.shown == 1,
            "a newer release did not ask the user");
    require(otaCalls.begin == 0 && restarts.empty(), "the check installed or restarted before the user chose");
    if (name == "check-alone-arms-nothing") {
      screen.onExit();  // Back, or the idle timeout
      require(restarts == std::vector<std::string>{"home"} && !bootNow().armed, "leaving unconfirmed armed the update");
      return;
    }
    screen.confirmPopup.chosen(name == "update-restarts-into-install" ? 1 : 0);
    require(screen.finishes == 1 && otaCalls.begin == 0, "the choice installed in this boot");
    screen.onExit();
    if (name == "cancel-restarts-home") {
      require(restarts == std::vector<std::string>{"home"} && !bootNow().armed, "Cancel armed the update boot");
      return;
    }
    require(restarts == std::vector<std::string>{"ota"}, "Update did not restart into the update boot");
    const auto boot = bootNow();
    require(boot.armed && !boot.dryRun(), "Update did not arm the install");
    return;
  }
  if (name == "update-boot-installs-without-asking") {
    update_boot::arm(otaBootSlot);
    OtaUpdateActivity screen(bootNow());
    screen.onWifiSelectionComplete(true);
    require(screen.confirmPopup.shown == 0, "the update boot asked again");
    require(screen.state == OtaUpdateActivity::SHUTTING_DOWN && otaCalls.setBoot == 1,
            "the update boot did not install");
    require(!bootNow().armed, "the install left the next boot armed");
    return;
  }
  if (name == "update-boot-failure-restarts-home" || name == "update-boot-without-wifi-restarts-home") {
    update_boot::arm(otaBootSlot);
    OtaUpdateActivity screen(bootNow());
    if (name == "update-boot-failure-restarts-home") {
      imageDrops = true;
      screen.onWifiSelectionComplete(true);
      require(screen.state == OtaUpdateActivity::FAILED && otaCalls.setBoot == 0, "a broken download installed");
    } else {
      WiFi.mode = WIFI_MODE_NULL;
      screen.onWifiSelectionComplete(false);
      require(screen.finishes == 1, "no Wi-Fi did not leave the update screen");
    }
    screen.onExit();
    require(restarts == std::vector<std::string>{"home"}, "a failed update boot did not restart to Home");
    require(!bootNow().armed, "a failed update boot armed another");
    return;
  }
  if (name == "install-comes-in-parts" || name == "broken-part-asks-again-from-reached-byte" ||
      name == "broken-parts-run-out" || name == "server-without-ranges-sends-whole-image" ||
      name == "range-ignored-after-first-part-fails" || name == "progress-frames-allocate-nothing") {
    image.resize(600 * 1024, 0);
    update_boot::arm(otaBootSlot);
    OtaUpdateActivity screen(bootNow());
    const size_t part = OtaUpdater::PART_BYTES;
    if (name == "broken-part-asks-again-from-reached-byte") {
      server.breakAt = 300000;
      server.breaks = 1;
    }
    if (name == "broken-parts-run-out") {
      server.breakAt = 300000;
      server.breaks = 100;
    }
    if (name == "server-without-ranges-sends-whole-image") server.ignoresRange = true;
    if (name == "range-ignored-after-first-part-fails") server.ignoresRangeAfterFirst = true;
    screen.paints = name == "progress-frames-allocate-nothing";
    screen.onWifiSelectionComplete(true);
    const auto& f = server.firsts;
    if (name == "broken-parts-run-out" || name == "range-ignored-after-first-part-fails") {
      require(screen.state == OtaUpdateActivity::FAILED && otaCalls.setBoot == 0 && otaCalls.abort == 1,
              "a download that cannot finish did not fail cleanly");
      if (name == "broken-parts-run-out")
        require(f.size() == 2 + OtaUpdater::MAX_PART_RETRIES && f.back() == 300000 &&
                    screen.updater.lastRetries() == OtaUpdater::MAX_PART_RETRIES + 1,
                "the retries did not stop at their limit");
      else
        require(f.size() == 2, "a server ignoring Range after the first part was asked again");
      return;
    }
    require(screen.state == OtaUpdateActivity::SHUTTING_DOWN && otaCalls.setBoot == 1 &&
                otaCalls.written == image.size(),
            "the image in parts did not install");
    if (name == "install-comes-in-parts" || name == "progress-frames-allocate-nothing")
      require(f == std::vector<size_t>({0, part, 2 * part, 3 * part}) && screen.updater.lastParts() == 4,
              "the image did not come in parts that follow each other");
    if (name == "broken-part-asks-again-from-reached-byte")
      require(f == std::vector<size_t>({0, part, 300000, 300000 + part}) && screen.updater.lastRetries() == 1,
              "the broken part was not asked again from the byte it reached");
    if (name == "server-without-ranges-sends-whole-image")
      require(f.size() == 1 && screen.updater.lastParts() == 1, "a whole body was not taken as the image");
    if (name == "progress-frames-allocate-nothing") {
      const auto& a = screen.frameAllocations;
      require(a.size() > 50, "the progress frames were not drawn");
      for (size_t i = 1; i < a.size(); ++i) require(a[i] == 0, "a progress frame allocated during the download");
      require(screen.renderer.clears == screen.renderer.displays, "a skipped percent still drew into the frame");
      require(screen.renderer.displays <= 53, "frames were drawn more often than every 2%");
      require(screen.firstFrameWarmed, "the first frame did not decode the digits before the byte line");
    }
    return;
  }
#ifdef TENOR_PRESS_PROBE
  if (name == "dry-run-series-one-run-a-boot") {
    fixtureTag = "v0.0.1";  // older than the running firmware: only the dry run takes it
    update_boot::arm(otaBootSlot, 3, 3);
    for (int run = 1; run <= 3; ++run) {
      const auto boot = bootNow();
      require(boot.armed && boot.dryRun() && boot.dryRunsTotal == 3, "the series lost its update boot");
      OtaUpdateActivity screen(boot);
      screen.onWifiSelectionComplete(true);
      screen.onExit();
      const std::string line = "run=" + std::to_string(run) + "/3";
      require(logSerial.text.find(line) != std::string::npos, "the run was not numbered in the series");
    }
    require(otaCalls.end == 3 && otaCalls.setBoot == 0, "the dry runs did not verify, or switched the boot slot");
    require(restarts == std::vector<std::string>({"ota", "ota", "home"}), "the series did not restart run by run");
    require(!bootNow().armed, "the series did not end");
    require(keptLine.find("run=3/3") != std::string::npos && keptLine.find("parts=1") != std::string::npos,
            "the last result line was not kept for the next boot");
    return;
  }
#endif
  throw std::runtime_error("unknown case");
}

// ---- The record slot: one block for every 16 KB record of the image.
static int allocs = 0, frees = 0;
static bool allocFails = false;
static void* countingMalloc(size_t size) {
  if (allocFails) return nullptr;
  ++allocs;
  return std::malloc(size);
}
static void countingFree(void* p) {
  ++frees;
  std::free(p);
}
void slotCases(const std::string& name) {
  tls_slot::Slot slot(countingMalloc, countingFree);
  const size_t record = 16384 + 17;  // a full application-data record as the server sends it
  if (name == "slot-one-block-for-every-record") {
    const void* first = nullptr;
    for (int i = 0; i < 400; ++i) {
      void* p = slot.take(record);
      require(p != nullptr, "a record was not served from the slot");
      if (!first) first = p;
      require(p == first, "a record got a different block");
      require(slot.give(p), "the slot's block was not recognised on free");
    }
    require(allocs == 1 && frees == 0 && slot.served() == 400, "the slot allocated more than once");
    slot.end();
    require(frees == 1, "ending the slot did not free its block");
    return;
  }
  if (name == "slot-leaves-other-requests-alone") {
    require(slot.take(4096) == nullptr, "a small request took the slot");
    require(slot.take(tls_slot::BYTES + 1) == nullptr, "an oversized request took the slot");
    void* p = slot.take(record);
    require(p && slot.take(record) == nullptr, "a second record while the first is in use took the slot");
    require(!slot.give(&allocs), "a pointer the slot does not own was kept");
    require(slot.fallbacks() == 2, "the fallbacks were not counted");
    slot.end();  // still in use: its owner frees it
    require(frees == 0, "the slot freed a block still in use");
    std::free(p);
    return;
  }
  if (name == "slot-takes-the-record-size-when-short") {
    static size_t limit = 0;
    tls_slot::Slot tight(
        [](size_t size) -> void* { return size <= limit ? std::malloc(size) : nullptr; }, std::free);
    limit = record;  // a heap with the record's size in one piece, not BYTES
    void* p = tight.take(record);
    require(p != nullptr && tight.blockBytes() == record, "the record's own size was not taken");
    tight.give(p);
    require(tight.take(record) == p && tight.take(record + 16) == nullptr, "the block was not kept at its size");
    tight.give(p);
    tight.end();
    return;
  }
  if (name == "slot-without-memory-falls-back") {
    allocFails = true;
    require(slot.take(record) == nullptr && slot.fallbacks() == 1, "a failed reservation was not a fallback");
    allocFails = false;
    void* p = slot.take(record);
    require(p != nullptr, "the slot did not try again");
    slot.give(p);
    slot.end();
    return;
  }
  throw std::runtime_error("unknown case");
}

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  const std::string group = argv[1], name = argv[2];
  try {
    if (group == "flag") flagCases(name);
    if (group == "screen") screenCases(name);
    if (group == "slot") slotCases(name);
    std::cout << "PASS " << name << '\n';
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL " << name << ": " << e.what() << '\n';
    return 1;
  }
}
