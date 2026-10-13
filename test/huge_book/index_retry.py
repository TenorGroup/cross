"""Run the production index step and reader retry policy with a refused heap."""
from pathlib import Path
import subprocess
import tempfile
import sys

root = Path(__file__).resolve().parents[2]


def function(path, signature):
    text = path.read_text()
    start = text.index(signature)
    opening = text.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


prefix = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
template <class... T> void logLine(T&&...) {}
#define LOG_PROBE(...) logLine(__VA_ARGS__)
#define LOG_ERR(...) logLine(__VA_ARGS__)
#define HEAP_MARK(...) logLine(__VA_ARGS__)
namespace heapfloor { inline std::atomic<uint32_t> refusals{0}; }
namespace buildscratch { bool available(size_t) { return false; } }
struct InflateStream { static size_t streamingBytes() { return 0; } };
struct GfxRenderer {
  unsigned frameBufferLoanCount() const { return 0; }
  struct FrameBufferLoan { explicit FrameBufferLoan(GfxRenderer&) {} };
};
unsigned long millis() { return 100; }
struct { unsigned getFreeHeap() { return 46080; } unsigned getMaxAllocHeap() { return 46080; } } ESP;
bool heapReady = false, cardFailed = false;
int mergeCalls = 0;
struct BookMetadataCache {
  using StopFn = bool (*)();
  bool partial = true, ready = false, refused = false;
  explicit BookMetadataCache(std::string = {}, std::string = {}) {}
  bool isLoaded() const { return true; }
  bool isPartial() const { return partial; }
  static bool deferredTocReady(const std::string&) { return true; }
  bool bookBinReady() const { return ready; }
  bool hasDeferredTocProgress(const std::string&) const { return false; }
  bool beginDeferredTocPass(const std::string& = {}, bool = false) { return true; }
  bool pauseDeferredTocPass() { return true; }
  bool endDeferredTocPass() { return true; }
  bool buildBookBinFromPart(const std::string&, StopFn) {
    ++mergeCalls;
    refused = !heapReady && !cardFailed;
    return ready = heapReady && !cardFailed;
  }
  bool wasHeapRefused() const { return refused; }
  static bool tocStepFits(size_t, size_t, size_t) { return true; }
  bool load(bool, StopFn) { partial = false; return true; }
  void removePartFiles() {}
  void discardBookBin() { ready = false; }
};
template <class T, class... A> auto makeUniqueNoThrow(A&&... args) {
  return std::make_unique<T>(std::forward<A>(args)...);
}
BookMetadataCache::StopFn stepStop = nullptr;
bool stepStopped = false;
bool askStepStop() { return stepStopped = stepStopped || (stepStop && stepStop()); }
class Epub {
 public:
  enum class IndexStep { Done, More, Stopped, Failed };
  std::unique_ptr<BookMetadataCache> bookMetadataCache = std::make_unique<BookMetadataCache>();
  std::string cachePath, filepath, tocNcxItem, tocNavItem;
  bool parseTocNavFile(BookMetadataCache*, BookMetadataCache::StopFn) { return true; }
  bool parseTocNcxFile(BookMetadataCache*, BookMetadataCache::StopFn) { return true; }
  IndexStep indexSome(BookMetadataCache::StopFn);
  bool indexStepInflates() const { return false; }
};
struct RenderLock { struct TryTake {}; explicit RenderLock(TryTake) {} bool acquired() { return true; } };
struct BackgroundBuildFrameGuard { template <class T> explicit BackgroundBuildFrameGuard(T&) {} };
struct HalPowerManager { struct Lock { Lock() {} }; };
struct { bool rawInputActive() { return false; } } gpio;
struct Section { bool isBuilding() { return false; } bool isBuildParked() { return false; } };
class EpubReaderActivity {
 public:
  std::shared_ptr<Epub> epub = std::make_shared<Epub>();
  std::shared_ptr<Section> section = std::make_shared<Section>();
  unsigned indexFailures = 0, indexStops = 0, indexRetryAtMs = 0;
  bool backgroundBuildSuspended = false;
  bool pageFrameShown = true;
  GfxRenderer renderer;
  int tocSpineCached = 0, completed = 0;
  static constexpr unsigned INDEX_RETRY_MS = 10000, INDEX_MAX_FAILURES = 3, INDEX_MAX_STOPS = 40;
  void suspendBackgroundBuild() {}
  void dropSectionsLaidOutWithoutToc() { ++completed; }
  void runIndexStep();
};
'''
suffix = r'''
int main() {
  EpubReaderActivity reader;
  for (int retry = 0; retry < 100; ++retry) reader.runIndexStep();
  assert(mergeCalls == 100);
  assert(reader.indexFailures == 0 && reader.indexStops == 0 && reader.indexRetryAtMs == 0);
  heapReady = true;
  reader.runIndexStep();
  reader.runIndexStep();
  assert(reader.completed == 1);
  assert(!reader.epub->bookMetadataCache->isPartial());
  EpubReaderActivity brokenCard;
  cardFailed = true;
  for (int retry = 0; retry < 3; ++retry) brokenCard.runIndexStep();
  assert(brokenCard.indexFailures == 3 && brokenCard.indexRetryAtMs > 0);
}
'''
epub_source = Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'lib/Epub/Epub.cpp'
source = prefix + function(epub_source, 'Epub::IndexStep Epub::indexSome(')
source += function(root / 'src/activities/reader/EpubReaderActivity.cpp', 'void EpubReaderActivity::runIndexStep()')
source += suffix
with tempfile.TemporaryDirectory(prefix='index-retry-') as folder:
    cpp = Path(folder) / 'retry.cpp'
    binary = Path(folder) / 'retry'
    cpp.write_text(source)
    subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('100 heap refusals: 0 failures, 0 stops; retry completes; card errors count as failures')
