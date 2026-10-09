#include <HalStorage.h>
#include <FontInstaller.h>
#include <FontPackInstaller.h>
#include <esp_rom_crc.h>
#include <I18n.h>
#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>
#include "activities/settings/FontDownloadProgress.h"
#include "activities/settings/FontDownloadReleaseGuard.h"
#include "activities/settings/FontDownloadHeap.h"
#include "network/WebDavReplace.h"
#include "network/HttpRangeTransfer.h"
#if __has_include("activities/settings/FontDownloadTransfer.h")
#include "activities/settings/FontDownloadTransfer.h"
#endif

#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
#define LOG_INF(...) ((void)0)
I18n& I18n::getInstance() { static I18n instance; return instance; }
const char* I18n::get(StrId id) const {
  if (id == StrId::STR_MEMORY_ERROR) return "memory";
#ifdef FONT_DOWNLOAD_PACK_IDS_PRESENT
  return id == StrId::STR_FONT_PACK_NO_SPACE ? "Need %u MB" : "error";
#else
  static_cast<void>(id);
  return "error";
#endif
}
unsigned long millis() { static unsigned long tick = 0; return tick += 25; }
void delay(unsigned long) {}
struct RenderLock {
  struct TryTake {};
  template <typename Value> explicit RenderLock(Value&&) {}
  bool acquired() const { return true; }
  void unlock() {}
};
struct Input {
  enum class Button { Back };
  bool back = false;
  void update(bool = false) {}
  bool isPressed(Button) const { return back; }
  bool wasPressed(Button) const { return back; }
  bool wasLongPressed(Button, int) const { return false; }
  bool wasHomeGesture() const { return false; }
};
using MappedInputManager = Input;
struct Renderer {
  Renderer* getFontCacheManager() { return nullptr; }
  void releaseSdFontCaches() {}
};
struct Esp {
  mutable unsigned freeReads = 0;
  unsigned refuseAt = 0;
  uint32_t getFreeHeap() const { return ++freeReads == refuseAt ? 39999 : 100000; }
  uint32_t getMaxAllocHeap() const { return 80000; }
  uint32_t getMinFreeHeap() const { return 60000; }
} ESP;
struct FontSystem {
  void markRegistryDirty() {}
  void releaseReaderForDownload(Renderer&) {}
} sdFontSystem;
void releaseBaseSettingsList() {}
namespace fui { struct ListItem {}; }
struct HalPowerManager {
  static inline int locks = 0;
  struct Lock {
    Lock() { ++locks; }
    ~Lock() { --locks; }
  };
};
std::vector<std::string> events;

class HttpDownloader {
 public:
  struct RangeSession {
    bool ready = false;
    bool hasTlsContext() const { return ready; }
  };
  enum DownloadError { OK, HTTP_ERROR, FILE_ERROR, ABORTED };
  using ProgressCallback = std::function<void(size_t, size_t)>;
  using DataCallback = std::function<bool(const uint8_t*, size_t)>;
  static constexpr uint32_t MIN_TLS_FREE_HEAP = 40000;
  static constexpr uint32_t MIN_TLS_MAX_ALLOC = 20000;
  static inline std::vector<uint8_t> payload;
  static inline int calls = 0;
  static inline bool tlsOpen = false;
  static inline bool failNetwork = false;
  static inline Input* input = nullptr;
  static inline bool cancelMidway = false;
  static inline int failAt = -1, cancelAt = -1;
  static inline std::function<void(size_t)> observe;
  struct TransferStats { uint32_t bytes = 0; int status = 0; };
  static inline int rangeCalls = 0, rangeFailureAt = -1;
  static inline size_t rangeFailureBytes = 37 * 1024;
  static inline bool fullResponse = false, laterFullResponse = false, rangePersistentFailure = false;
  static inline std::string failedUrl;
  static inline std::vector<std::pair<size_t, size_t>> ranges;
  static bool fetchRange(const std::string& url, size_t first, size_t last, const DataCallback& data,
                        const char*, ProgressCallback progress, bool* cancel, TransferStats* stats, bool* whole,
                        RangeSession* session = nullptr) {
    assert(!tlsOpen);
    ++rangeCalls;
    ++calls;
    ranges.emplace_back(first, last);
    events.push_back("download:" + url.substr(url.rfind('/') + 1));
    if (observe) observe(0);
    tlsOpen = true;
    if (session) session->ready = true;
    *whole = fullResponse || (laterFullResponse && first > 0);
    stats->status = *whole ? 200 : 206;
    if (calls == failAt) failedUrl = url;
    bool success = !failNetwork && url != failedUrl && !(*whole && first > 0);
    const size_t end = *whole ? payload.size() : std::min(last + 1, payload.size());
    for (size_t offset = first; success && offset < end;) {
      if ((cancelMidway || calls == cancelAt) && offset > first) input->back = true;
      if (progress) progress(offset - first, end - first);
      if (cancel && *cancel) { success = false; break; }
      size_t count = std::min<size_t>(payload.size() <= 1024 ? 17 : 1024, end - offset);
      if (first == 2 * 192 * 1024 && offset < 567863)
        count = std::min(count, size_t{567863} - offset);
      if (rangeCalls == rangeFailureAt)
        count = std::min(count, rangeFailureBytes - stats->bytes);
      success = data(payload.data() + offset, count);
      if (!success) break;
      offset += count;
      stats->bytes += count;
      if (progress) progress(offset - first, end - first);
      if (observe) observe(offset - first);
      if (rangeCalls == rangeFailureAt && stats->bytes >= rangeFailureBytes) success = false;
      if (rangePersistentFailure && first >= 192 * 1024) success = false;
    }
    if (rangePersistentFailure && first >= 192 * 1024) success = false;
    tlsOpen = false;
    return success;
  }
  static bool fetchUrl(const std::string& url, const DataCallback& data, const std::string& = "",
                       const std::string& = "", const char* = nullptr, bool = true,
                       ProgressCallback progress = nullptr, bool* cancel = nullptr) {
    ++calls;
    events.push_back("download:" + url.substr(url.rfind('/') + 1));
    if (observe) observe(0);
    tlsOpen = true;
    bool success = !failNetwork && calls != failAt;
    for (size_t offset = 0; success && offset < payload.size();) {
      if ((cancelMidway || calls == cancelAt) && offset > 0) input->back = true;
      if (progress) progress(offset, payload.size());
      if (cancel && *cancel) { success = false; break; }
      const size_t count = std::min<size_t>(17, payload.size() - offset);
      success = data(payload.data() + offset, count);
      offset += count;
      if (progress) progress(offset, payload.size());
      if (observe) observe(offset);
    }
    tlsOpen = false;
    return success;
  }
  static DownloadError downloadToFile(const std::string& url, const char* path, ProgressCallback progress = nullptr,
                                      bool* cancel = nullptr, const std::string& user = "",
                                      const std::string& password = "", bool downgrade = false) {
    if (downgrade) { ++calls; return HTTP_ERROR; }
    HalFile file;
    if (!Storage.openFileForWrite("HTTP", path, file)) return FILE_ERROR;
    const bool success = fetchUrl(url, [&](const uint8_t* bytes, size_t count) {
      return file.write(bytes, count) == count;
    }, user, password, nullptr, true, progress, cancel);
    file.close();
    return success ? OK : (cancel && *cancel ? ABORTED : HTTP_ERROR);
  }
};

class FontDownloadActivity {
 public:
  enum State { FAMILY_LIST, DOWNLOADING, COMPLETE, ERROR };
  struct ManifestFile { std::string name, url; uint32_t size = 0, crc32 = 0; };
  struct ManifestFamily {
    std::string name = "Example";
    uint32_t fileStart = 0, fileCount = 1, totalSize = 0;
    bool installed = true, hasUpdate = true;
    const char* failureReason = nullptr;
    uint32_t requiredMb = 0;
  };
  State state_ = FAMILY_LIST;
  std::vector<ManifestFamily> families_{ManifestFamily{}};
  std::vector<ManifestFile> files_;
  std::vector<int> filteredIndices_{0};
  std::vector<std::string> rowLabels_;
  std::vector<fui::ListItem> rowItems_;
  FontInstaller fontInstaller_;
  Renderer renderer;
  Input mappedInput;
  int downloadingFamilyIndex_ = 0;
  size_t fileProgress_ = 0, fileTotal_ = 0, currentFileIndex_ = 0, currentFileTotal_ = 1;
  bool cancelRequested_ = false, goHomeRequested_ = false, rowsDirty_ = false, installingPack_ = false;
  bool batchRunning_ = false, batchResult_ = false;
  bool terminalIdleTimerStarted_ = false;
  uint64_t batchTotalBytes_ = 0, batchDownloadedBytes_ = 0;
  uint32_t familyDownloadedBytes_ = 0, streamedBytes_ = 0;
  unsigned batchFamilyIndex_ = 0, batchFamilyCount_ = 0, batchSuccessCount_ = 0, batchFailureCount_ = 0;
  int resultFailureIndex_ = 0;
  const char* failureReason_ = nullptr;
  uint32_t requiredMb_ = 0;
  fontdownload::ProgressRenderGate progressRenderGate_;
  std::string baseUrl_ = "https://example.test/", downloadUrl_, errorMessage_;
  int installs = 0, commits = 0;
  bool failInstall = false;
  int failInstallAt = -1;
  bool installPowerCorrect = true;
  std::vector<uint8_t> installedBytes;
  const char* str(const std::string& text) const { return text.c_str(); }
  void requestUpdate(bool = false) {}
  void requestUpdateAndWait() {}
  void onGoHome() { state_ = FAMILY_LIST; }
  void returnToFamilyList(fontdownload::ReleaseButton) { state_ = FAMILY_LIST; }
  void downloadFamily(ManifestFamily& family);
  bool prepareDownloadHeap();
  void downloadAll();
  void updateAll();
  void downloadSelected(bool updates);
  void updateDownloadProgress(size_t downloaded, size_t total);
  void waitForDownloadPaint();
  bool installDownloadedPack(const char* path);
  FontPackInstaller::Result installFixturePack(const char* path) {
    ++installs;
    installPowerCorrect &= HalPowerManager::locks > 0 && installingPack_ && state_ == DOWNLOADING;
    events.push_back("install:" + std::string(path).substr(std::string(path).rfind('/') + 1));
    assert(!HttpDownloader::tlsOpen);
    assert(state_ != COMPLETE);
    assert(!Storage.exists((std::string(path) + ".tmp").c_str()));
    if (failInstall || installs == failInstallAt) return FontPackInstaller::Result::IO_ERROR;
    std::ifstream stream(Storage.path(path), std::ios::binary);
    installedBytes.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    const auto name = std::filesystem::path(path).stem().string();
    std::ofstream(Storage.path(("/fonts/" + name + "/old.txt").c_str())) << "committed";
    Storage.remove(path);
    ++commits;
    return FontPackInstaller::Result::OK;
  }
  static bool computeFileCrc32(const char* path, uint32_t& checksum) {
    std::ifstream stream(Storage.path(path), std::ios::binary);
    const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    if (!stream && bytes.empty()) return false;
    checksum = esp_rom_crc32_le(0, bytes.data(), bytes.size());
    return true;
  }
};
#include "production_activity.inc"

int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string scenario(argv[1]);
  cardRoot = std::filesystem::temp_directory_path() / ("down56-" + scenario);
  std::filesystem::remove_all(cardRoot);
  std::filesystem::create_directories(cardRoot / "fonts/Example");
  std::ofstream(cardRoot / "fonts/Example/old.txt") << "old";
  FontDownloadActivity activity;
  FontPackInstaller::installCallback = [&](const char* path) { return activity.installFixturePack(path); };
  HttpDownloader::input = &activity.mappedInput;
  HttpDownloader::payload.assign(100, 7);
  const bool legacy = scenario.starts_with("legacy") || scenario == "rangelegacy" || scenario == "rangelegacywrite";
  if (scenario.starts_with("range")) {
    HttpDownloader::payload.resize(scenario == "range70" ? 13740612 : 3 * 192 * 1024 + 123);
    for (size_t offset = 0; offset < HttpDownloader::payload.size(); ++offset)
      HttpDownloader::payload[offset] = static_cast<uint8_t>(offset * 31 + offset / 251);
  }
  if (legacy) std::memcpy(HttpDownloader::payload.data(), "CPFONT\0\0", 8);
  if (legacy) std::ofstream(cardRoot / "fonts/Example/Example_14.cpfont") << "old cpfont";
  auto checksum = esp_rom_crc32_le(0, HttpDownloader::payload.data(), HttpDownloader::payload.size());
  if (scenario.starts_with("range")) {
    activity.files_.push_back({legacy ? "Example_14.cpfont" : "Example.cpfontpack", "",
                              static_cast<uint32_t>(HttpDownloader::payload.size()), checksum});
    const bool aborted = scenario == "rangecancel" || scenario == "rangewritecancel";
    const bool failed = scenario == "rangelater200" || scenario == "rangebudget" || scenario == "rangecrc" ||
                        scenario == "rangeshort" || scenario == "rangewritebudget" || scenario == "rangewriteinvalid";
    if (scenario == "rangeretry") HttpDownloader::rangeFailureAt = 2;
    if (scenario == "rangesilent") {
      HttpDownloader::rangeFailureAt = 3;
      HttpDownloader::rangeFailureBytes = 174647;
    }
    if (scenario == "rangewrite" || scenario == "rangepartialwrite" || scenario == "rangewritebudget" ||
        scenario == "rangelegacywrite" || scenario == "rangewritecancel" || scenario == "rangewriteinvalid") {
      failWriteOffset = 567863;
      failWritePrefix = scenario == "rangepartialwrite" || scenario == "rangewriteinvalid" ? 512 : 0;
      failWriteReported = scenario == "rangewriteinvalid" ? 1 : 0;
      failWritePersistent = scenario == "rangewritebudget";
    }
    if (scenario == "range200") HttpDownloader::fullResponse = true;
    if (scenario == "rangelater200") HttpDownloader::laterFullResponse = true;
    if (scenario == "rangebudget") HttpDownloader::rangePersistentFailure = true;
    if (scenario == "rangecrc") activity.files_[0].crc32 ^= 1;
    if (scenario == "rangeshort") ++activity.files_[0].size;
    if (aborted) HttpDownloader::cancelAt = scenario == "rangewritecancel" ? 4 : 2;
    bool progressCorrect = true;
    size_t previousProgress = 0;
    activity.batchRunning_ = true;
    activity.batchTotalBytes_ = activity.files_[0].size;
    HttpDownloader::observe = [&](size_t) {
      progressCorrect &= activity.fileProgress_ == activity.streamedBytes_ &&
                         activity.fileProgress_ >= previousProgress &&
                         activity.batchDownloadedBytes_ + activity.fileProgress_ == activity.streamedBytes_;
      previousProgress = activity.fileProgress_;
    };
    activity.downloadFamily(activity.families_[0]);
    bool passed = HttpDownloader::rangeCalls > 0 && progressCorrect && !HttpDownloader::tlsOpen;
    for (const auto& range : HttpDownloader::ranges)
      passed &= range.second >= range.first && range.second - range.first + 1 <= 192 * 1024;
    if (aborted || failed) {
      passed &= activity.commits == 0 && activity.installs == 0 && activity.families_[0].installed &&
                activity.state_ == (aborted ? FontDownloadActivity::FAMILY_LIST : FontDownloadActivity::ERROR);
      if (aborted) passed &= HttpDownloader::rangeCalls == (scenario == "rangewritecancel" ? 4 : 2);
      if (scenario == "rangelater200") passed &= HttpDownloader::rangeCalls == 2;
      if (scenario == "rangebudget") passed &= HttpDownloader::rangeCalls == 7;
      if (scenario == "rangewritebudget") passed &= HttpDownloader::rangeCalls == 8;
      if (scenario == "rangewriteinvalid") passed &= HttpDownloader::rangeCalls == 3;
      std::ifstream old(cardRoot / "fonts/Example/old.txt");
      const std::string preserved{std::istreambuf_iterator<char>(old), std::istreambuf_iterator<char>()};
      passed &= preserved == "old";
    } else {
      std::ifstream stream(Storage.path(legacy ? "/fonts/Example/Example_14.cpfont" : "/fonts/Example.cpfontpack"),
                           std::ios::binary);
      const std::vector<uint8_t> stored = legacy
          ? std::vector<uint8_t>{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()}
          : activity.installedBytes;
      passed &= stored == HttpDownloader::payload && activity.streamedBytes_ == stored.size() &&
                activity.batchDownloadedBytes_ == stored.size() &&
                esp_rom_crc32_le(0, stored.data(), stored.size()) == checksum &&
                activity.installs == (legacy ? 0 : 1) && activity.families_[0].installed;
      if (scenario == "range200") passed &= HttpDownloader::rangeCalls == 1;
      else if (scenario == "range70") passed &= HttpDownloader::rangeCalls == 70;
      else passed &= HttpDownloader::rangeCalls == 4;
      if (scenario == "rangeretry")
        passed &= HttpDownloader::ranges.size() > 2 && HttpDownloader::ranges[2].first == 192 * 1024 + 37 * 1024;
      if (scenario == "rangesilent" || scenario == "rangewrite" || scenario == "rangepartialwrite" ||
          scenario == "rangelegacywrite")
        passed &= HttpDownloader::ranges.size() > 3 &&
                  HttpDownloader::ranges[3].first == 567863 + (scenario == "rangepartialwrite" ? 512 : 0);
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(cardRoot))
      if (entry.path().extension() == ".tmp" || entry.path().extension() == ".davtmp") passed = false;
    std::filesystem::remove_all(cardRoot);
    std::cout << scenario << " " << (passed ? "PASS" : "FAIL") << " ranges=" << HttpDownloader::rangeCalls
              << " bytes=" << activity.streamedBytes_ << '\n';
    return passed ? 0 : 1;
  }
  if (scenario.starts_with("batch")) {
    activity.families_.clear();
    activity.filteredIndices_.clear();
    for (int index = 0; index < 3; ++index) {
      const auto name = std::string("Family") + std::to_string(index);
      std::filesystem::create_directories(cardRoot / "fonts" / name);
      std::ofstream(cardRoot / "fonts" / name / "old.txt") << "old";
      const uint32_t size = index == 1 ? 900 : 100;
      std::vector<uint8_t> bytes(size, 7);
      activity.families_.push_back({name, static_cast<uint32_t>(index), 1, size,
                                   scenario != "batchorder", true});
      activity.filteredIndices_.push_back(index);
      activity.files_.push_back({name + ".cpfontpack", "", size,
                                esp_rom_crc32_le(0, bytes.data(), size)});
    }
    bool bytesCorrect = true, powerCorrect = true;
    HttpDownloader::observe = [&](size_t bytes) {
      const int index = activity.downloadingFamilyIndex_;
      if (bytes == 0) {
        HttpDownloader::payload.assign(index == 1 ? 900 : 100, 7);
      }
      if (bytes == 0) {
        const uint64_t completed = index == 0 ? 0 : index == 1 ? 100 : 1000;
        bytesCorrect &= activity.batchTotalBytes_ == 1100 && activity.batchDownloadedBytes_ == completed;
      }
      powerCorrect &= HalPowerManager::locks > 0 && activity.batchRunning_ &&
                      activity.state_ == FontDownloadActivity::DOWNLOADING;
      const uint64_t completed = index == 0 ? 0 : index == 1 ? 100 : 1000;
      if (bytes > 0) bytesCorrect &= activity.batchDownloadedBytes_ + activity.fileProgress_ == completed + bytes;
    };
    if (scenario == "batchcrc") activity.files_[1].crc32 ^= 1;
    if (scenario == "batchnetwork") HttpDownloader::failAt = 2;
    if (scenario == "batchinstall") activity.failInstallAt = 2;
    if (scenario == "batchcancel") HttpDownloader::cancelAt = 2;
    if (scenario == "batchspace") {
      Storage.available = 4 * 1024 * 1024 + 1000;
    }
    if (scenario == "batchorder") activity.downloadAll();
    else activity.updateAll();
    bool passed = true;
    if (scenario == "batchorder" || scenario == "batchupdate") {
      passed = activity.commits == 3 && events == std::vector<std::string>{
          "download:Family0.cpfontpack", "install:Family0.cpfontpack", "download:Family1.cpfontpack",
          "install:Family1.cpfontpack", "download:Family2.cpfontpack", "install:Family2.cpfontpack"};
    } else if (scenario == "batchbytes") {
      passed = bytesCorrect && activity.batchTotalBytes_ == 1100 && activity.batchDownloadedBytes_ == 1100 &&
               activity.batchSuccessCount_ == 3 && activity.batchFamilyCount_ == 3;
    } else if (scenario == "batchpower") {
      passed = powerCorrect && activity.installPowerCorrect && HalPowerManager::locks == 0 && !activity.batchRunning_;
    } else if (scenario == "batchcancel") {
      passed = activity.commits == 1 && HttpDownloader::calls == 2 && activity.families_[0].installed &&
               !activity.families_[0].hasUpdate && activity.families_[1].hasUpdate &&
               activity.families_[2].hasUpdate && activity.state_ == FontDownloadActivity::FAMILY_LIST;
    } else {
      passed = activity.commits == 2 && activity.batchSuccessCount_ == 2 && activity.batchFailureCount_ == 1 &&
               activity.families_[1].failureReason && activity.state_ == FontDownloadActivity::COMPLETE &&
               activity.families_[1].installed && activity.families_[1].hasUpdate &&
               !activity.families_[0].hasUpdate && !activity.families_[2].hasUpdate;
      std::ifstream old(cardRoot / "fonts/Family1/old.txt");
      std::string contents;
      old >> contents;
      passed &= contents == "old";
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(cardRoot))
      if (entry.path().extension() == ".tmp") passed = false;
    std::filesystem::remove_all(cardRoot);
    std::cout << scenario << " " << (passed ? "PASS" : "FAIL") << " commits=" << activity.commits << '\n';
    return passed ? 0 : 1;
  }
  activity.files_.push_back({legacy ? "Example_14.cpfont" : "Example.cpfontpack", "", 100, checksum});
  if (scenario == "success") activity.families_[0].installed = false;
  if (scenario == "crc" || scenario == "legacycrc") activity.files_[0].crc32 ^= 1;
  if (scenario == "short") activity.files_[0].size += 1;
  if (scenario == "space") Storage.available = 0;
  if (scenario == "cancel" || scenario == "legacycancel") HttpDownloader::cancelMidway = true;
  if (scenario == "network") HttpDownloader::failNetwork = true;
  if (scenario == "installer") activity.failInstall = true;
  if (scenario == "heap") ESP.refuseAt = 1;
  if (scenario == "heap-late") ESP.refuseAt = 2;
  activity.downloadFamily(activity.families_[0]);
  bool passed = false;
  if (scenario == "success") {
    passed = activity.state_ == FontDownloadActivity::COMPLETE && activity.installs == 1 && activity.commits == 1 &&
             activity.families_[0].installed && HttpDownloader::calls == 1;
  } else if (scenario == "legacy") {
    passed = activity.state_ == FontDownloadActivity::COMPLETE && activity.installs == 0 && HttpDownloader::calls == 1 &&
             Storage.exists("/fonts/Example/Example_14.cpfont");
  } else {
    std::ifstream original(cardRoot / "fonts/Example/old.txt");
    std::string content;
    original >> content;
    passed = content == "old" && activity.families_[0].installed && activity.commits == 0 &&
             activity.installs == (scenario == "installer" ? 1 : 0) &&
             activity.state_ == (scenario == "cancel" || scenario == "legacycancel"
                                     ? FontDownloadActivity::FAMILY_LIST : FontDownloadActivity::ERROR);
    if (legacy) {
      std::ifstream font(cardRoot / "fonts/Example/Example_14.cpfont");
      const std::string previous{std::istreambuf_iterator<char>(font), std::istreambuf_iterator<char>()};
      passed = passed && previous == "old cpfont";
    }
    if (scenario == "space") passed = passed && HttpDownloader::calls == 0;
    if (scenario == "heap" || scenario == "heap-late") {
      passed &= HttpDownloader::calls == 0 && activity.errorMessage_ == "memory";
    }
  }
  for (const auto& entry : std::filesystem::recursive_directory_iterator(cardRoot))
    if (entry.path().extension() == ".tmp" || entry.path().extension() == ".davtmp") passed = false;
  std::filesystem::remove_all(cardRoot);
  std::cout << scenario << " " << (passed ? "PASS" : "FAIL") << " downloads=" << HttpDownloader::calls
            << " installs=" << activity.installs << " commits=" << activity.commits << '\n';
  return passed ? 0 : 1;
}
