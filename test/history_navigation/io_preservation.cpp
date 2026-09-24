#define STATS_SCHEMA_HELPERS_ONLY
#include "schema_preservation.cpp"
#include "ReadingStatsStore.cpp"

namespace {
void unavailableMain(const std::string& kind) {
  readFault = {};
  seed();
  const std::string path = ReadingStatsStore::getFilePath();
  const std::string current = document("4");
  check(Storage.writeFile(path.c_str(), kind == "oversize" ? current + std::string(32769, ' ') : current), "seed current");
  check(Storage.writeFile((path + ".bak").c_str(), "{\"schema\":4,\"ngay\":[[20260912,2,3]]}"), "seed stale bak");
  check(Storage.writeFile((path + ".davbak").c_str(), "{\"schema\":4,\"ngay\":[[20260911,1,2]]}"), "seed stale davbak");
  const auto original = snapshot();
  readFault.path = path;
  readFault.fullPath = true;
  if (kind == "open") readFault.openFails = true;
  if (kind == "error" || kind == "eof") readFault.failAfter = 7;
  if (kind == "eof") readFault.result = 0;
  if (kind == "close") readFault.closeFails = true;
  check(!READING_STATS.loadFromFile(), kind + " load refuses stale backup");
  check(kind == "oversize" || readFault.hits > 0, kind + " fault actually reached");
  check(!READING_STATS.statisticsReadable, kind + " store unreadable");
  check(!READING_STATS.saveToFile(), kind + " save refuses");
  check(READING_STATS.resetStatistics(false) == ReadingStatsStore::ResetResult::Failed,
        kind + " habits reset refuses");
  check(READING_STATS.resetStatistics(true) == ReadingStatsStore::ResetResult::Failed,
        kind + " all reset refuses");
  check(snapshot() == original, kind + " every artifact byte retained");
  readFault = {};
  if (kind == "oversize") {
    check(!READING_STATS.loadFromFile(), "oversize remains refused without injected fault");
    check(snapshot() == original, "oversize retry retains all bytes");
  } else {
    check(READING_STATS.loadFromFile(), kind + " clean retry loads current");
    JsonDocument actual;
    READING_STATS.toJson(actual);
    check(actual["ngay"][0][0].as<unsigned>() == 20260913 && actual["ngay"][0][1].as<unsigned>() == 12,
          kind + " clean retry retained newer statistics");
  }
}

void completedShortReads() {
  readFault = {};
  seed();
  check(Storage.writeFile(ReadingStatsStore::getFilePath(), document("4")), "seed short reads");
  readFault.path = ReadingStatsStore::getFilePath();
  readFault.fullPath = true;
  readFault.shortLimit = 3;
  check(READING_STATS.loadFromFile(), "positive short reads load completely");
  readFault = {};
}

void injectReadFault(const std::string& path, const std::string& kind, size_t length) {
  readFault = {};
  readFault.path = path;
  readFault.fullPath = true;
  readFault.openFails = kind == "open";
  readFault.closeFails = kind == "close";
  if (kind == "error" || kind == "eof") readFault.failAfter = 7;
  if (kind == "late-error") readFault.failAfter = length + 7;
  if (kind == "eof") readFault.result = 0;
}

void directWriteRefuses(const std::string& kind, bool perBook) {
  readFault = {};
  seed();
  const std::string path = perBook ? bookFile("/Books/io-test.epub") : ReadingStatsStore::getFilePath();
  const std::string current = document("4");
  const std::string originalBytes = current + std::string(kind == "oversize" ? 32769 : 128, ' ');
  check(Storage.writeFile(path.c_str(), originalBytes), "seed direct writer main");
  check(Storage.writeFile((path + ".bak").c_str(), "{\"saved\":1}"), "seed direct writer backup");
  check(Storage.writeFile((path + ".davbak").c_str(), "{\"saved\":0}"), "seed direct writer recovery");
  const auto original = snapshot();
  JsonDocument next;
  next["schema"] = 4;
  next["saved"] = 99;
  injectReadFault(path, kind, current.size());
  const std::string label = (perBook ? "book " : "global ") + kind;
  check(!writeSnapshot(path, next), label + " direct write refuses unavailable main");
  check(kind == "oversize" || readFault.hits > 0, label + " direct write fault reached");
  check(snapshot() == original, label + " direct write retains every byte and creates no staging");
  readFault = {};
  if (kind != "oversize") {
    check(writeSnapshot(path, next), label + " direct write clean retry succeeds");
    JsonDocument actual;
    check(readSnapshot(path, actual) && actual["saved"].as<unsigned>() == 99,
          label + " direct write retry stores new document");
    check(Storage.readFile((path + ".bak").c_str()) == originalBytes, label + " retry retains prior main as backup");
  }
}

void recoveryUnavailable(const std::string& suffix, const std::string& kind) {
  readFault = {};
  seed();
  const std::string main = ReadingStatsStore::getFilePath();
  const std::string path = main + suffix;
  check(Storage.writeFile(main.c_str(), "{broken"), "seed invalid main");
  if (suffix == ".davbak") {
    check(Storage.writeFile((main + ".bak").c_str(), "{\"schema\":4,\"ngay\":[[20000101,1,1]]}"), "seed old bak");
  } else if (suffix == ".bak.davbak") {
    check(Storage.writeFile((main + ".bak").c_str(), "{broken backup"), "seed invalid bak");
  }
  const std::string current = document("4");
  check(Storage.writeFile(path.c_str(), current + std::string(kind == "oversize" ? 32769 : 128, ' ')),
        "seed unavailable recovery");
  const auto original = snapshot();
  injectReadFault(path, kind, current.size());
  const std::string label = suffix + " " + kind;
  check(!READING_STATS.loadFromFile(), label + " unavailable recovery refuses load");
  check(kind == "oversize" || readFault.hits > 0, label + " recovery fault reached");
  check(!READING_STATS.saveToFile(), label + " refuses save");
  check(snapshot() == original, label + " recovery preserves every byte");
  readFault = {};
  if (kind != "oversize") {
    check(READING_STATS.loadFromFile(), label + " recovery clean retry succeeds");
    JsonDocument actual;
    READING_STATS.toJson(actual);
    check(actual["ngay"][0][0].as<unsigned>() == 20260913, label + " recovery retries newest readable copy");
  }
}

void exactBoundAndAllocation() {
  for (const auto* suffix : {"", ".davbak", ".bak", ".bak.davbak"}) {
    readFault = {};
    seed();
    const std::string main = ReadingStatsStore::getFilePath();
    const std::string path = main + suffix;
    check(Storage.writeFile(main.c_str(), "{broken"), "seed bound invalid main");
    const std::string current = document("4");
    check(Storage.writeFile(path.c_str(), current + std::string(32768 - current.size(), ' ')), "seed exact 32768 bound");
    check(READING_STATS.loadFromFile(), std::string(suffix) + " accepts exact bound");
    JsonDocument actual;
    READING_STATS.toJson(actual);
    check(actual["ngay"][0][0].as<unsigned>() == 20260913, std::string(suffix) + " exact bound retains counters");
  }
  seed();
  const std::string path = ReadingStatsStore::getFilePath();
  check(Storage.writeFile(path.c_str(), document("4")), "seed allocation main");
  check(Storage.writeFile((path + ".bak").c_str(), document("3")), "seed allocation backup");
  const auto original = snapshot();
  struct Deny : ArduinoJson::Allocator {
    void* allocate(size_t) override { ++hits; return nullptr; }
    void deallocate(void*) override {}
    void* reallocate(void*, size_t) override { ++hits; return nullptr; }
    unsigned hits = 0;
  } deny;
  JsonDocument noHeap(&deny);
  check(!readSnapshot(path, noHeap), "allocation failure refuses read");
  check(deny.hits > 0, "real ArduinoJson allocation fault reached");
  check(snapshot() == original, "allocation failure retains all bytes");
}

// writeSnapshot() moves the main file aside only once .tmp is whole, so a cut between its
// two renames leaves the newest snapshot in .tmp beside the older .bak.
void cutBetweenRenames(bool perBook) {
  readFault = {};
  seed();
  const std::string path = perBook ? bookFile("/Books/io-test.epub") : ReadingStatsStore::getFilePath();
  const std::string label = perBook ? "book" : "global";
  check(Storage.writeFile((path + ".bak").c_str(), "{\"schema\":4,\"saved\":1}"), "seed older bak");
  check(Storage.writeFile((path + ".tmp").c_str(), "{\"schema\":4,\"saved\":2}"), "seed newer tmp");
  JsonDocument actual;
  check(readSnapshot(path, actual) && actual["saved"].as<unsigned>() == 2,
        label + " cut between renames loads the newer tmp, not the older bak");
  const auto original = snapshot();
  injectReadFault(path + ".tmp", "open", 0);
  check(!readSnapshot(path, actual), label + " unreadable tmp refuses the older bak");
  check(readFault.hits > 0, label + " tmp fault reached");
  check(snapshot() == original, label + " unreadable tmp keeps every byte");
  readFault = {};
}

// A readable main file stays first even beside a readable .tmp. The reset path removes the
// main file and .bak but leaves .tmp, so a .tmp from a save cut before a reset is older
// than the main file the reset wrote; nothing in the file tells the two cases apart.
void resetKeepsMainOverOlderTmp() {
  readFault = {};
  seed();
  const std::string path = ReadingStatsStore::getFilePath();
  check(Storage.writeFile(path.c_str(), document("4")), "seed main before reset");
  check(Storage.writeFile((path + ".tmp").c_str(), "{\"schema\":4,\"ngay\":[[20200101,99,99]]}"),
        "seed tmp from a save cut before the reset");
  check(READING_STATS.loadFromFile(), "load before reset");
  check(READING_STATS.resetStatistics(true) == ReadingStatsStore::ResetResult::Complete, "reset completes");
  check(Storage.exists((path + ".tmp").c_str()), "reset leaves the older tmp");
  check(READING_STATS.loadFromFile(), "load after reset");
  JsonDocument actual;
  READING_STATS.toJson(actual);
  check(actual["ngay"].size() == 0, "the older tmp does not bring back statistics the reset cleared");
}
}  // namespace

int main() {
  for (const auto* kind : {"open", "error", "eof", "close", "oversize"}) unavailableMain(kind);
  completedShortReads();
  for (const auto* kind : {"open", "error", "eof", "late-error", "close", "oversize"}) {
    directWriteRefuses(kind, false);
    directWriteRefuses(kind, true);
    for (const auto* suffix : {".davbak", ".bak", ".bak.davbak"}) recoveryUnavailable(suffix, kind);
  }
  exactBoundAndAllocation();
  cutBetweenRenames(false);
  cutBetweenRenames(true);
  resetKeepsMainOverOlderTmp();
  std::filesystem::remove_all(fixtureRoot);
  std::cout << "RESULT " << checks - failures << '/' << checks << " assertions passed\n";
  return failures ? 1 : 0;
}
