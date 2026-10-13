"""The background TOC step keeps the X3 heap floor: the production index step and reader step,
run over the heap the X3 measured before the step (heap59 m1) with the step's allocations as the
device took them. Pass `<Epub.cpp> <EpubReaderActivity.cpp>` to run other sources (the base)."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

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


header = (root / 'lib/Epub/Epub/BookMetadataCache.h').read_text()
budget = dict(re.findall(r'static constexpr size_t (TOC_STEP_\w+) = ([^;]+);', header))
fits = r'''
  static bool tocStepFits(size_t, size_t, size_t);
  static constexpr size_t TOC_STEP_HEAP = %s, TOC_STEP_BLOCKS = %s;''' % (
    budget.get('TOC_STEP_HEAP', '0'), budget.get('TOC_STEP_BLOCKS', '0'))
fits_body = ''
if budget:
    fits_body = function(root / 'lib/Epub/Epub/BookMetadataCache.cpp', 'bool BookMetadataCache::tocStepFits(')

prefix = r'''
#include <HeapFloor.h>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <utility>
template <class... T> void logLine(T&&...) {}
#define LOG_PROBE(...) logLine(__VA_ARGS__)
#define LOG_ERR(...) logLine(__VA_ARGS__)
#define LOG_INF(...) logLine(__VA_ARGS__)
#define HEAP_MARK(...) logLine(__VA_ARGS__)
unsigned long millis() { return 100; }

// The X3 heap as the step sees it: free bytes, and the largest free block, which every block from
// 4 KB up is cut from (on the X3 the 43 KB inflater left 9.716 B of a 53.236 B block). Blocks go
// through the door as on the device: one that would leave the heap under the floor is refused.
struct Heap {
  size_t free = 0, largest0 = 0, big = 0, minFree = SIZE_MAX, minLargest = SIZE_MAX;
  size_t largest() const { size_t l = largest0 > big ? largest0 - big : 0; return l < free ? l : free; }
  void reset(size_t f, size_t l) { free = f; largest0 = l; big = 0; minFree = minLargest = SIZE_MAX; }
  bool take(size_t n) {
    if (n > free || (n >= heapfloor::kBigBytes && n > largest())) return false;
    free -= n;
    if (n >= heapfloor::kBigBytes) big += n;
    if (free < minFree) minFree = free;
    if (largest() < minLargest) minLargest = largest();
    if (!heapfloor::stands(n, free, largest())) { give(n); heapfloor::refusals++; return false; }
    return true;
  }
  void give(size_t n) { free += n; if (n >= heapfloor::kBigBytes) big -= n; }
} heap;
struct { unsigned getFreeHeap() { return heap.free; } unsigned getMaxAllocHeap() { return heap.largest(); } } ESP;

// The lent framebuffer (X3: 792 x 528 / 8) and its one claimant, as lib/Memory/BuildScratch.cpp.
namespace buildscratch {
bool lent = false, claimed = false;
bool available(size_t n) { return lent && !claimed && n <= 52272; }
}
// The inflater: tinfl state (10,5 KB on the device) and the 32 KB window.
constexpr size_t INFLATE = 10560 + 32768;
struct InflateStream { static size_t streamingBytes() { return INFLATE; } };
// What the step took besides the inflater on the X3: up to 28 KB (rc1-4), two blocks of 4 KB in it
// and the rest in small ones.
constexpr size_t REST_BIG = 4096, REST_SMALL = 28 * 1024 - 2 * REST_BIG;
// A parser block beyond the budget (a nav with one very long token), for the door's refusal.
size_t extraBig = 0;

struct GfxRenderer {
  unsigned loans = 0;
  unsigned frameBufferLoanCount() const { return loans; }
  struct FrameBufferLoan {
    explicit FrameBufferLoan(GfxRenderer& r) { r.loans++; buildscratch::lent = true; }
    ~FrameBufferLoan() { buildscratch::lent = false; }
  };
};

bool tocDone = false;
int tocRuns = 0;
struct BookMetadataCache {
  using StopFn = bool (*)();
  bool partial = true;
  explicit BookMetadataCache(std::string = {}, std::string = {}) {}
  bool isLoaded() const { return true; }
  bool isPartial() const { return partial; }
  static bool deferredTocReady(const std::string&) { return tocDone; }
  bool bookBinReady() const { return false; }
  bool hasDeferredTocProgress(const std::string&) const { return false; }
  bool beginDeferredTocPass(const std::string& = {}, bool = false) { return true; }
  bool pauseDeferredTocPass() { return true; }
  bool endDeferredTocPass() { tocDone = true; return true; }
  bool buildBookBinFromPart(const std::string&, StopFn) { return false; }
  bool wasHeapRefused() const { return true; }
  bool load(bool, StopFn) { return true; }
  void removePartFiles() {}
  void discardBookBin() {}
''' + fits + r'''
};
''' + fits_body + r'''
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
  std::string cachePath, filepath, tocNcxItem, tocNavItem = "nav.xhtml";
  // The nav streamed as on the device: the inflater, then the pass and the parser at their peak.
  bool parseTocNavFile(BookMetadataCache*, BookMetadataCache::StopFn) const {
    ++tocRuns;
    size_t taken[32], n = 0;
    const auto take = [&](size_t bytes) { return heap.take(bytes) && (taken[n++] = bytes, true); };
    const bool scratch = buildscratch::available(INFLATE);
    if (scratch) buildscratch::claimed = true;
    bool ok = (scratch || take(INFLATE)) && take(REST_BIG) && take(REST_BIG);
    for (size_t left = REST_SMALL; ok && left; left -= 1024) ok = take(1024);
    if (ok && extraBig) ok = take(extraBig);
    while (n) heap.give(taken[--n]);  // the stream's blocks go back when it ends
    buildscratch::claimed = false;
    return ok;
  }
  bool parseTocNcxFile(BookMetadataCache*, BookMetadataCache::StopFn) const { return false; }
  bool indexStepInflates() const;
  IndexStep indexSome(BookMetadataCache::StopFn);
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
  unsigned indexFailures = 0, indexStops = 0, indexRetryAtMs = 0, repaints = 0;
  bool backgroundBuildSuspended = false, pageFrameShown = true;
  int tocSpineCached = 0;
  GfxRenderer renderer;
  static constexpr unsigned INDEX_RETRY_MS = 10000, INDEX_MAX_FAILURES = 3, INDEX_MAX_STOPS = 40;
  void suspendBackgroundBuild() {}
  void dropSectionsLaidOutWithoutToc() {}
  void requestUpdate() { ++repaints; }
  void runIndexStep();
};
'''
suffix = r'''
// One reader step over a heap of `freeBytes` free, `largest` the largest block (reported numbers).
void step(EpubReaderActivity& reader, size_t freeBytes, size_t largest) {
  tocDone = false;
  heap.reset(freeBytes, largest);
  reader.runIndexStep();
}

int main() {
  constexpr size_t floorFree = heapfloor::kFreeFloor + 8 * 1024;  // the floor and a clear margin
  // X3 heap59 m1, index_step_begin of the 5.000-chapter book: free 78.368 B, largest 53.236 B.
  EpubReaderActivity reader;
  step(reader, 78368, 53236);
  printf("m1: toc_done=%d min_free=%zu min_largest=%zu refusals=%u repaints=%u frame=%d\n", tocDone ? 1 : 0,
         heap.minFree, heap.minLargest, heapfloor::refusals.load(), reader.repaints, reader.pageFrameShown ? 1 : 0);
  assert(tocDone && reader.indexFailures == 0);
  assert(heap.minFree >= floorFree && heap.minLargest >= heapfloor::kLargestFloorReported + 8 * 1024);
  assert(heapfloor::refusals.load() == 0);
  // The page stays on the panel: nothing repaints it, and the framebuffer is no longer the page.
  assert(reader.repaints == 0 && !reader.pageFrameShown);

  // A heap that cannot hold the step even with the framebuffer: refused before anything is taken,
  // the reader comes back, no failure counted.
  EpubReaderActivity tight;
  const int runs = tocRuns;
  for (int i = 0; i < 5; ++i) step(tight, 36000, 30000);
  printf("tight: toc_done=%d runs=%d failures=%u refusals=%u\n", tocDone ? 1 : 0, tocRuns - runs,
         tight.indexFailures, heapfloor::refusals.load());
  assert(!tocDone && tocRuns == runs && tight.indexFailures == 0 && heap.minFree == SIZE_MAX);

  // The door refuses a parser block mid-pass: the pass keeps what it wrote and the reader comes
  // back later; neither the NCX nor a failure is tried on a heap failure.
  EpubReaderActivity starved;
  extraBig = 40 * 1024;
  const unsigned refusedBefore = heapfloor::refusals.load();
  for (int i = 0; i < 5; ++i) step(starved, 78368, 53236);
  extraBig = 0;
  printf("starved: toc_done=%d failures=%u refusals=%u min_free=%zu\n", tocDone ? 1 : 0, starved.indexFailures,
         heapfloor::refusals.load() - refusedBefore, heap.minFree);
  assert(!tocDone && starved.indexFailures == 0 && starved.indexRetryAtMs == 0);
  assert(heapfloor::refusals.load() - refusedBefore == 5);
}
'''
epub_source = Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'lib/Epub/Epub.cpp'
reader_source = Path(sys.argv[2]) if len(sys.argv) > 2 else root / 'src/activities/reader/EpubReaderActivity.cpp'
source = prefix + function(epub_source, 'Epub::IndexStep Epub::indexSome(')
if 'bool Epub::indexStepInflates(' in epub_source.read_text():
    source += function(epub_source, 'bool Epub::indexStepInflates(')
else:
    source += 'bool Epub::indexStepInflates() const { return false; }\n'
source += function(reader_source, 'void EpubReaderActivity::runIndexStep()')
source += suffix
with tempfile.TemporaryDirectory(prefix='toc-floor-') as folder:
    cpp = Path(folder) / 'floor.cpp'
    binary = Path(folder) / 'floor'
    cpp.write_text(source)
    subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-I', str(root / 'lib/Memory'), str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('TOC step at the X3 heap of heap59 m1: on the floor with a clear margin, no repaint; a tight heap is refused')
