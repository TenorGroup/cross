struct CheckFailure : std::runtime_error { using std::runtime_error::runtime_error; };
#define CHECK(value) do { if (!(value)) throw CheckFailure(std::string(#value) + " at line " + std::to_string(__LINE__)); } while (false)

static std::unique_ptr<Txt> book(const std::string& text = "one\ntwo\nthree\nfour\nfive\nsix\n") {
  Storage = {};
  Storage.put("/book.txt", text);
  auto txt = std::make_unique<Txt>(); txt->fileSize = text.size(); return txt;
}

static void txtReadError() {
  auto txt = book("abcdefgh");
  Storage.files.at("/book.txt")->readPlan = {-1};
  uint8_t data[8] = {};
  CHECK(!txt->readContent(data, 0, sizeof(data)));
}
static void txtShortEof() {
  auto txt = book("abcdefgh");
  Storage.files.at("/book.txt")->readPlan = {3, 0};
  uint8_t data[8] = {};
  CHECK(!txt->readContent(data, 0, sizeof(data)));
}
static void txtShortCompletes() {
  auto txt = book("abcdefgh");
  Storage.files.at("/book.txt")->readPlan = {3, 2, 3};
  uint8_t data[8] = {};
  CHECK(txt->readContent(data, 0, sizeof(data)));
  CHECK(std::memcmp(data, "abcdefgh", 8) == 0);
}
static void zipReadError() {
  auto txt = book("abcdefgh");
  ZipFile zip; zip.file.data = Storage.files.at("/book.txt");
  zip.file.data->readPlan = {-1}; zip.stat.uncompressedSize = 8;
  Print out; out.allowedSpan = 4;
  CHECK(!zip.readFileToStream("book", out, 4));
  CHECK(out.maxSpan <= 4);
}
static void zipShortCompletes() {
  auto txt = book("abcdefgh");
  ZipFile zip; zip.file.data = Storage.files.at("/book.txt");
  zip.file.data->readPlan = {2, 1, 4, 1}; zip.stat.uncompressedSize = 8;
  Print out; out.allowedSpan = 4;
  CHECK(zip.readFileToStream("book", out, 4));
  CHECK(std::string(out.bytes.begin(), out.bytes.end()) == "abcdefgh");
}
static void zipEarlyStop() {
  auto txt = book("abcdefgh");
  ZipFile zip; zip.file.data = Storage.files.at("/book.txt"); zip.stat.uncompressedSize = 8;
  Print out; out.limit = 2; CHECK(zip.readFileToStream("book", out, 4, true));
  zip.file.pos = 0; CHECK(!zip.readFileToStream("book", out, 4, false));
}
static void incompleteIndex() {
  TxtReaderActivity reader; reader.txt = book();
  auto data = Storage.files.at("/book.txt");
  data->readPlan = {static_cast<int>(data->bytes.size()), 0};
  GfxRenderer gfx;
  reader.initializeReader(gfx);
  CHECK(!reader.initialized);
  CHECK(Storage.writeOpens == 0);
  CHECK(reader.progressLoads == 0);
  data->readPlan.clear(); data->readIndex = 0;
  reader.initializeReader(gfx);
  CHECK(reader.initialized);
  CHECK(reader.totalPages == 3);
  CHECK(Storage.exists("/cache/index.bin"));
}
static void pageReadRejectsUnfilledBuffer() {
  TxtReaderActivity reader; reader.txt = book(); reader.linesPerPage = 2;
  reader.viewportWidth = 16; reader.viewportHeight = 2;
  reader.cachedLineHeight = reader.cachedInkHeight = 1;
  Storage.files.at("/book.txt")->readPlan = {3, 0};
  GfxRenderer gfx; std::vector<std::string> lines; size_t next = 123;
  CHECK(!reader.loadPageAtOffset(gfx, 0, lines, next));
  CHECK(gfx.textScans == 0);
}

static void zipReadZero() {
  auto txt = book("abcdefgh");
  ZipFile zip; zip.file.data = Storage.files.at("/book.txt");
  zip.file.data->readPlan = {0}; zip.stat.uncompressedSize = 8;
  Print out; out.allowedSpan = 4;
  CHECK(!zip.readFileToStream("book", out, 4));
  CHECK(out.bytes.empty());
}
static void zipShortThenError() {
  auto txt = book("abcdefgh");
  ZipFile zip; zip.file.data = Storage.files.at("/book.txt");
  zip.file.data->readPlan = {2, -1}; zip.stat.uncompressedSize = 8;
  Print out; out.allowedSpan = 4;
  CHECK(!zip.readFileToStream("book", out, 4, true));
  CHECK(out.maxSpan <= 4);
}
static void zipInflateErrorEarlyStop() {
  auto txt = book("abcdefgh");
  ZipFile zip; zip.file.data = Storage.files.at("/book.txt");
  zip.stat = {ZIP_METHOD_DEFLATED, 8, 8, 0};
  Print out; out.allowedSpan = 4; out.limit = 2;
  InflateStream::enabled = true; InflateStream::configuredProduced = 4;
  InflateStream::configuredStatus = InflateStream::Status::Error;
  CHECK(!zip.readFileToStream("book", out, 4, true));
  InflateStream::enabled = false;
}
static void txtReadZero() {
  auto txt = book("abcdefgh"); Storage.files.at("/book.txt")->readPlan = {0};
  uint8_t bytes[8] = {}; CHECK(!txt->readContent(bytes, 0, sizeof(bytes)));
}
static void txtShortThenError() {
  auto txt = book("abcdefgh"); Storage.files.at("/book.txt")->readPlan = {3, -1};
  uint8_t bytes[8] = {}; CHECK(!txt->readContent(bytes, 0, sizeof(bytes)));
}
static void readSeekFailure() {
  auto txt = book("abcdefgh"); Storage.files.at("/book.txt")->seekOk = false;
  uint8_t bytes[8] = {}; CHECK(!txt->readContent(bytes, 0, sizeof(bytes)));
  ZipFile zip; zip.file.data = Storage.files.at("/book.txt"); zip.stat.uncompressedSize = 8;
  Print out; CHECK(!zip.readFileToStream("book", out, 4)); CHECK(out.bytes.empty());
}
static void zipZeroChunk() {
  auto txt = book("abcdefgh"); ZipFile zip; zip.file.data = Storage.files.at("/book.txt");
  zip.stat.uncompressedSize = 8; Print out;
  CHECK(!zip.readFileToStream("book", out, 0)); CHECK(out.bytes.empty());
}
static void txtRangeBounds() {
  auto txt = book("abcdefgh"); uint8_t bytes[8] = {};
  CHECK(!txt->readContent(bytes, std::numeric_limits<size_t>::max(), 4));
  CHECK(!txt->readContent(bytes, 2, std::numeric_limits<size_t>::max()));
  CHECK(!txt->readContent(nullptr, 0, 1));
  CHECK(txt->readContent(nullptr, 8, 0));
}

static void set32(std::vector<uint8_t>& bytes, size_t pos, uint32_t value) {
  CHECK(pos + sizeof(value) <= bytes.size()); std::memcpy(bytes.data() + pos, &value, sizeof(value));
}
static void cacheFault(const std::function<void(std::vector<uint8_t>&)>& mutate, bool probe = false) {
  TxtReaderActivity reader; reader.txt = book(); GfxRenderer gfx;
  reader.initializeReader(gfx); CHECK(reader.initialized); CHECK(reader.totalPages == 3);
  auto file = Storage.files.at("/cache/index.bin");
  CHECK(file->bytes.size() == 46 + 3 * 4);
  mutate(file->bytes);
  bool accepted = false;
  if (probe) {
    AllocationProbe allocation(4096);
    accepted = reader.loadPageIndexCache();
    CHECK(rejectedAllocations == 0);
    CHECK(largestAllocation <= 4096);
  } else {
    AllocationProbe allocation(4096);
    accepted = reader.loadPageIndexCache();
  }
  CHECK(!accepted);
}
static void cacheCountHuge() { cacheFault([](auto& bytes) { set32(bytes, 42, 0xffffffffu); }, true); }
static void cacheCountZero() { cacheFault([](auto& bytes) { set32(bytes, 42, 0); }); }
static void cacheTruncatedHeader() { cacheFault([](auto& bytes) { bytes.resize(45); }); }
static void cacheTruncatedOffsets() { cacheFault([](auto& bytes) { bytes.resize(bytes.size() - 1); }); }
static void cacheFirstNonzero() { cacheFault([](auto& bytes) { set32(bytes, 46, 1); }); }
static void cacheDuplicateOffset() { cacheFault([](auto& bytes) { set32(bytes, 50, 0); }); }
static void cacheBackwardsOffset() { cacheFault([](auto& bytes) { set32(bytes, 54, 1); }); }
static void cacheOffsetBeyondBook() { cacheFault([](auto& bytes) { set32(bytes, 54, 999999); }); }
static void cacheTruncatedAtEveryByte() {
  TxtReaderActivity reader; reader.txt = book(); GfxRenderer gfx; reader.initializeReader(gfx);
  auto file = Storage.files.at("/cache/index.bin"); const auto bytes = file->bytes;
  CHECK(bytes.size() == 58);
  for (size_t length = 0; length < bytes.size(); ++length) {
    file->bytes.assign(bytes.begin(), bytes.begin() + length);
    AllocationProbe allocation(4096);
    CHECK(!reader.loadPageIndexCache());
  }
  file->bytes = bytes; CHECK(reader.loadPageIndexCache());
}
static void cacheReadError() {
  TxtReaderActivity reader; reader.txt = book(); GfxRenderer gfx; reader.initializeReader(gfx);
  auto file = Storage.files.at("/cache/index.bin"); file->readPlan = {-1};
  CHECK(!reader.loadPageIndexCache());
}
template <class T> static bool checkedSave(T& reader) {
  if constexpr (std::is_same_v<decltype(reader.savePageIndexCache()), bool>) return reader.savePageIndexCache();
  else { reader.savePageIndexCache(); return true; }
}
static void cacheSaveFault(bool failSync, bool failRename = false) {
  TxtReaderActivity reader; reader.txt = book(); GfxRenderer gfx; reader.initializeReader(gfx);
  const auto prior = Storage.files.at("/cache/index.bin")->bytes;
  if (failRename) Storage.renameOk = false;
  else if (failSync) Storage.syncOk = false;
  else Storage.writeLimit = 0;
  const bool saved = checkedSave(reader);
  CHECK(Storage.exists("/cache/index.bin"));
  CHECK(Storage.files.at("/cache/index.bin")->bytes == prior);
  CHECK(!saved);
}
static void cacheSaveWriteFailure() { cacheSaveFault(false); }
static void cacheSaveSyncFailure() { cacheSaveFault(true); }
static void cacheSaveRenameFailure() { cacheSaveFault(false, true); }

static void mallocFailureThenRetry() {
  TxtReaderActivity reader; reader.txt = book(); GfxRenderer gfx;
  chunkAllocations = 0; failChunkAllocationAt = 2;
  reader.initializeReader(gfx);
  CHECK(chunkAllocations == 2);
  CHECK(!reader.initialized); CHECK(Storage.writeOpens == 0); CHECK(reader.progressLoads == 0);
  failChunkAllocationAt = 0;
  reader.initializeReader(gfx);
  CHECK(reader.initialized); CHECK(reader.totalPages == 3);
}
static void arrayGrowthFailureThenRetry() {
  TxtReaderActivity reader; reader.txt = book(); GfxRenderer gfx;
  arrayAllocationCalls = 0; failArrayAllocationAt = 3;
  reader.initializeReader(gfx);
  CHECK(arrayAllocationCalls == 3);
  CHECK(!reader.initialized); CHECK(Storage.writeOpens == 0); CHECK(reader.progressLoads == 0);
  failArrayAllocationAt = 0;
  reader.initializeReader(gfx);
  CHECK(reader.initialized); CHECK(reader.totalPages == 3);
}
static void backupRecovery(bool corruptPrimary) {
  TxtReaderActivity reader; reader.txt = book(); GfxRenderer gfx; reader.initializeReader(gfx);
  auto primary = Storage.files.at("/cache/index.bin"); const auto bytes = primary->bytes;
  Storage.files["/cache/index.bin.davbak"] = primary;
  if (corruptPrimary) Storage.put("/cache/index.bin", "bad cache");
  else Storage.remove("/cache/index.bin");
  Storage.put("/cache/index.bin.tmp", "uncommitted incomplete write");
  reader.initialized = false;
  reader.initializeReader(gfx);
  CHECK(reader.initialized); CHECK(reader.totalPages == 3);
  CHECK(Storage.writeOpens == 1);
  if (!corruptPrimary) CHECK(Storage.files.at("/cache/index.bin")->bytes == bytes);
}
static void backupMissingPrimary() { backupRecovery(false); }
static void backupCorruptPrimary() { backupRecovery(true); }

static void canonicalCacheRemovesStaleBackup() {
  TxtReaderActivity reader; reader.txt = book(); GfxRenderer gfx; reader.initializeReader(gfx);
  Storage.put("/cache/index.bin.davbak", "stale incomplete backup");
  CHECK(reader.loadPageIndexCache());
  CHECK(!Storage.exists("/cache/index.bin.davbak"));
  CHECK(checkedSave(reader));
}
static void offsetCapacityMeasurement() {
  TxtReaderActivity reader;
  size_t oldCapacity = 0;
  size_t peakBytes = 0;
  AllocationProbe probe(std::numeric_limits<size_t>::max());
  for (uint32_t i = 0; i < 4097; ++i) {
    CHECK(reader.addPageOffset(i));
    if (reader.pageOffsetCapacity != oldCapacity) {
      peakBytes = std::max(peakBytes, (oldCapacity + reader.pageOffsetCapacity) * sizeof(uint32_t));
      oldCapacity = reader.pageOffsetCapacity;
    }
  }
  CHECK(reader.pageOffsetCount == 4097); CHECK(reader.pageOffsetCapacity == 8192);
  CHECK(largestAllocation == 32768);
  std::cout << "MEASURE offset_count=" << reader.pageOffsetCount << " capacity=" << reader.pageOffsetCapacity
            << " allocated_bytes=" << reader.pageOffsetCapacity * sizeof(uint32_t)
            << " largest_allocation=" << largestAllocation << " growth_overlap_bytes=" << peakBytes << '\n';
}

static void inkKeepsIndexHit() {
  auto first = book();
  const auto size = first->fileSize;
  for (bool preview : {false, true}) {
    TxtReaderActivity initial;initial.txt = std::make_unique<Txt>();initial.txt->fileSize = size;initial.preview = preview;
    GfxRenderer gfx;initial.initializeReader(gfx);CHECK(initial.initialized);
    const auto path = preview ? "/cache/preview_index.bin" : "/cache/index.bin";
    const auto bytes = Storage.files.at(path)->bytes;
    for (int level = 0; level < 6; ++level) for (int aa = 0; aa < 2; ++aa) {
      SETTINGS.readerInkWeight = level;SETTINGS.textAntiAliasing = aa;
      TxtReaderActivity reopened;reopened.txt = std::make_unique<Txt>();reopened.txt->fileSize = size;reopened.preview = preview;
      GfxRenderer hit;reopened.initializeReader(hit);
      CHECK(reopened.initialized);CHECK(hit.textScans == 0);CHECK(reopened.totalPages == initial.totalPages);
      CHECK(Storage.files.at(path)->bytes == bytes);
      for (uint32_t index = 0; index < initial.pageOffsetCount; ++index)
        CHECK(reopened.pageOffsets[index] == initial.pageOffsets[index]);
    }
  }
}

int main(int argc, char** argv) {
  const std::string filter = argc > 1 ? argv[1] : "all";
  int total = 0, failures = 0;
  const std::vector<std::pair<const char*, std::function<void()>>> cases = {
    {"INK index and preview index remain HIT", inkKeepsIndexHit},
    {"READ-03 negative read", txtReadError}, {"READ-03 short then EOF", txtShortEof},
    {"READ-03 short reads complete", txtShortCompletes}, {"IO-01 negative stored read", zipReadError},
    {"IO-01 short reads complete", zipShortCompletes}, {"IO-01 early stop contract", zipEarlyStop},
    {"READ-02 failure then retry", incompleteIndex}, {"READ-03 page parser rejects unfilled bytes", pageReadRejectsUnfilledBuffer},
    {"IO-01 zero read", zipReadZero}, {"IO-01 partial then read error preserves failure", zipShortThenError},
    {"IO-01 inflate error retains failure before early stop", zipInflateErrorEarlyStop},
    {"READ-03 zero read", txtReadZero}, {"READ-03 partial then read error", txtShortThenError},
    {"IO-01 READ-03 failed seek", readSeekFailure}, {"IO-01 zero chunk", zipZeroChunk},
    {"READ-03 requested range bounds", txtRangeBounds},
    {"CACHE-01 huge count before allocation", cacheCountHuge}, {"CACHE-01 zero pages", cacheCountZero},
    {"CACHE-01 truncated header", cacheTruncatedHeader}, {"CACHE-01 truncated offsets", cacheTruncatedOffsets},
    {"CACHE-01 first offset nonzero", cacheFirstNonzero}, {"CACHE-01 duplicate offsets", cacheDuplicateOffset},
    {"CACHE-01 backwards offsets", cacheBackwardsOffset},
    {"CACHE-01 out of range offset", cacheOffsetBeyondBook}, {"CACHE-01 cache read error", cacheReadError},
    {"CACHE-01 truncation at every byte", cacheTruncatedAtEveryByte},
    {"READ-02 cache save write failure preserves prior", cacheSaveWriteFailure},
    {"READ-02 cache save sync failure preserves prior", cacheSaveSyncFailure},
    {"READ-02 cache save rename failure preserves prior", cacheSaveRenameFailure},
    {"READ-02 page allocation failure then retry", mallocFailureThenRetry},
    {"READ-02 offset array allocation failure then retry", arrayGrowthFailureThenRetry},
    {"READ-02 backup recovery missing primary", backupMissingPrimary},
    {"READ-02 backup recovery corrupt primary", backupCorruptPrimary},
    {"READ-02 validated cache clears stale backup", canonicalCacheRemovesStaleBackup},
    {"CACHE-01 offset capacity measurement", offsetCapacityMeasurement},
  };
  for (const auto& test : cases) {
    if (filter != "all" && std::string(test.first).find(filter) == std::string::npos) continue;
    ++total;
    arrayAllocationCalls = failArrayAllocationAt = chunkAllocations = failChunkAllocationAt = 0;
    allocationLimit = std::numeric_limits<size_t>::max();
    InflateStream::enabled = false;
    try { test.second(); std::cout << "PASS " << test.first << '\n'; }
    catch (const std::exception& error) { ++failures; std::cout << "FAIL " << test.first << ": " << error.what() << '\n'; }
  }
  std::cout << "RESULT " << total - failures << "/" << total << " passed\n";
  return failures || total == 0 ? 1 : 0;
}
