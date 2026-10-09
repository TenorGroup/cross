#include <ArduinoJson.h>
#include <Memory.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "HeapCap.h"
#include "FontManifestValidation.h"
#include "FontDownloadHeap.h"
#include <components/lists/list.h>

#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define FONTS_MANIFEST_VERSION 1
#define FONT_MANIFEST_URL "https://example.test/fonts.json"

struct JsonAllocator : ArduinoJson::Allocator {
  size_t live = 0;
  void* allocate(size_t bytes) override {
    auto* memory = heapcap::allocate(bytes, true);
    if (memory) live += bytes;
    return memory;
  }
  void deallocate(void* memory) override {
    if (memory) live -= (static_cast<heapcap::Header*>(memory) - 1)->size;
    heapcap::release(memory);
  }
  void* reallocate(void* memory, size_t bytes) override {
    if (!memory) return allocate(bytes);
    const auto oldBytes = (static_cast<heapcap::Header*>(memory) - 1)->size;
    auto* replacement = allocate(bytes);
    if (!replacement) return nullptr;
    std::memcpy(replacement, memory, std::min(bytes, oldBytes));
    deallocate(memory);
    return replacement;
  }
} jsonAllocator;

struct SdCardFont { std::unique_ptr<char[]> metadata = makeUniqueNoThrow<char[]>(5952); };
struct GfxRenderer {
  std::map<int, int> fallbackFontMap_;
  bool hasFallbackFont(int fontId) const {
    return std::any_of(fallbackFontMap_.begin(), fallbackFontMap_.end(),
                       [fontId](const auto& entry) { return entry.second == fontId; });
  }
  void removeFont(int) {}
  void insertFont(int, int) {}
  GfxRenderer* getFontCacheManager() { return this; }
  void releaseSdFontCaches() {}
};
struct SdCardFontManager {
  struct LoadedFont { SdCardFont* font; int fontId; };
  std::vector<LoadedFont> loaded_;
  std::unique_ptr<int> builtinFamily_;
  int builtinId_ = 0;
  unsigned loadedPointSize_ = 18, loadedWeight_ = 3;
  bool readerReleased_ = false;
  void releaseReaderForDownload(GfxRenderer& renderer);
  ~SdCardFontManager() { for (auto& font : loaded_) delete font.font; }
};
#include "manager.inc"

struct FontSystem {
  SdCardFontManager manager;
  void markRegistryDirty() {}
  void releaseReaderForDownload(GfxRenderer& renderer) { manager.releaseReaderForDownload(renderer); }
} sdFontSystem;
void releaseBaseSettingsList() {}
struct RenderLock { template<class Owner> explicit RenderLock(Owner&) {} };
struct Esp {
  uint32_t largestLimit = UINT32_MAX;
  uint32_t getFreeHeap() const { return static_cast<uint32_t>(heapcap::available()); }
  uint32_t getMaxAllocHeap() const { return std::min(getFreeHeap(), largestLimit); }
} ESP;
struct HalFile {
  const std::string* bytes = nullptr;
  size_t offset = 0;
  int read() { return offset < bytes->size() ? static_cast<unsigned char>((*bytes)[offset++]) : -1; }
  size_t readBytes(char* output, size_t length) {
    const auto count = std::min(length, bytes->size() - offset);
    std::memcpy(output, bytes->data() + offset, count);
    offset += count;
    return count;
  }
  size_t fileSize() const { return 1; }
  void close() {}
};
struct StorageFixture {
  std::string bytes;
  bool openFileForRead(const char*, const char*, HalFile& file) {
    file.bytes = &bytes;
    return true;
  }
  void remove(const char*) {}
} Storage;
struct SdCardFontRegistry { static const char* findFamilyRoot(const char*) { return nullptr; } };
struct FontInstaller {
  static constexpr size_t MAX_FONT_PATH_SIZE = 256;
  static bool isValidFamilyName(const char*) { return true; }
  static bool buildFontPath(const char*, const char*, char*, size_t) { return true; }
};
namespace fontdownload {
enum class ManifestFileKind { Invalid, SingleFont, Pack };
inline ManifestFileKind classifyManifestFile(const char*) { return ManifestFileKind::Pack; }
inline bool packMatchesFamily(const char*, const char*) { return true; }
}
namespace fui = freeink::ui;
struct HttpDownloader {
  enum Result { OK };
  static constexpr uint32_t MIN_TLS_FREE_HEAP = 40000, MIN_TLS_MAX_ALLOC = 20000;
  static Result downloadToFile(const char*, const char*, std::nullptr_t) { return OK; }
};
enum { STR_MEMORY_ERROR, STR_INVALID_FONT_MANIFEST };
const char* tr(int) { return "error"; }

class FontDownloadActivity {
 public:
  using StrRef = uint32_t;
  struct ManifestFile { StrRef name = 0; uint32_t size = 0, crc32 = 0; };
  struct ManifestFamily {
    StrRef name = 0, description = 0;
    uint32_t fileStart = 0, fileCount = 0, totalSize = 0, scriptMask = 0;
    bool installed = false, hasUpdate = false;
    const char* failureReason = nullptr;
    uint32_t requiredMb = 0;
  };
  static constexpr size_t MAX_SCRIPT_GROUPS = 32;
  GfxRenderer renderer;
  std::string baseUrl_, downloadUrl_, errorMessage_;
  std::unique_ptr<char[]> stringArena_;
  uint32_t arenaUsed_ = 0, arenaCapacity_ = 0, fileEntryCount_ = 0;
  std::vector<ManifestFamily> families_;
  std::unique_ptr<ManifestFile[]> files_;
  std::vector<StrRef> scriptGroupLabels_;
  std::vector<int> filteredIndices_;
  std::vector<std::string> rowLabels_;
  std::vector<freeink::ui::ListItem> rowItems_;
  bool rowsDirty_ = false;
  const char* str(StrRef offset) const { return stringArena_.get() + offset; }
  void clearManifest();
  bool internString(const char*, StrRef&);
  bool fetchAndParseManifest();
  bool prepareDownloadHeap();
};
#include "production.inc"

int main() {
  Storage.bytes = R"({"version":1,"baseUrl":"https://example.test/fonts/","scriptGroups":[{"tag":"latin","label":"Latin"}],"families":[)";
  for (unsigned family = 0; family < 7; ++family) {
    if (family) Storage.bytes += ',';
    const auto name = "Example" + std::to_string(family);
    Storage.bytes += "{\"name\":\"" + name + "\",\"description\":\"Font description\",\"scripts\":[\"latin\"],\"files\":[{\"name\":\"" + name + ".cpfont.zip\",\"size\":100,\"crc32\":1}]}";
  }
  Storage.bytes += "]}";
  heapcap::reset(60000);
  {
    FontDownloadActivity activity;
    sdFontSystem.manager.loaded_.push_back({new SdCardFont, 42});
    assert(activity.fetchAndParseManifest());
    assert(jsonAllocator.live == 0);
    assert(activity.families_.size() == 7 && activity.fileEntryCount_ == 7);
    const auto steady = heapcap::live;
    const auto peak = heapcap::peak;
    std::printf("parsed json_live=%zu steady=%zu peak=%zu rows=%zu/%zu arena=%u files=%u\n",
                jsonAllocator.live, steady, peak, activity.rowLabels_.capacity(), activity.rowItems_.capacity(),
                activity.arenaCapacity_, activity.fileEntryCount_);
    assert(activity.rowLabels_.capacity() == 0 && activity.rowItems_.capacity() == 0);
    activity.rowLabels_.assign(9, std::string(80, 'x'));
    activity.rowItems_.resize(9);
    assert(activity.prepareDownloadHeap());
    assert(activity.rowLabels_.capacity() == 0 && activity.rowItems_.capacity() == 0 && activity.rowsDirty_);
    assert(sdFontSystem.manager.loaded_.empty());
    assert(ESP.getFreeHeap() >= 40000 && ESP.getMaxAllocHeap() >= 20000);
    assert(heapcap::aborts == 0 && heapcap::refusals == 0);
    std::printf("prepared free=%u largest_model=%u released=%zu aborts=%u refusals=%u\n",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap(), steady - heapcap::live, heapcap::aborts, heapcap::refusals);
    sdFontSystem.manager.loaded_.push_back({new SdCardFont, 43});
    sdFontSystem.manager.readerReleased_ = false;
    activity.renderer.fallbackFontMap_[1] = 43;
    sdFontSystem.manager.releaseReaderForDownload(activity.renderer);
    assert(sdFontSystem.manager.loaded_.size() == 1);
    assert(!fontdownload::hasTlsHeadroom(39999, 20000, 40000, 20000));
    assert(!fontdownload::hasTlsHeadroom(40000, 19999, 40000, 20000));
    assert(fontdownload::hasTlsHeadroom(40000, 20000, 40000, 20000));
    assert(!fontdownload::canAllocateCatalog(4096, 14335, 12288));
    assert(!fontdownload::canAllocateCatalog(4096, 14336, 12287));
    assert(fontdownload::canAllocateCatalog(4096, 14336, 12288));
    auto pressure = makeUniqueNoThrow<char[]>(ESP.getFreeHeap() - 39999);
    assert(ESP.getFreeHeap() == 39999);
    assert(pressure && !activity.prepareDownloadHeap());
    pressure.reset();
    pressure = makeUniqueNoThrow<char[]>(ESP.getFreeHeap() - 40000);
    ESP.largestLimit = 19999;
    assert(pressure && !activity.prepareDownloadHeap());
    ESP.largestLimit = 20000;
    assert(activity.prepareDownloadHeap());
    std::puts("TLS floors: 39999/20000 refused, 40000/19999 refused, 40000/20000 accepted");
  }
  assert(jsonAllocator.live == 0);
  heapcap::stop();
  std::puts("GREEN: response released, rows released, reader released, UI fallback retained, original floors enforced");
}
