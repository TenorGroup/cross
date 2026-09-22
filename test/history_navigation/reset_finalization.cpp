#define main measure_dataset_main
#include "measure.cpp"
#undef main

#include "ReadingStatsStore.cpp"

#include <map>

namespace {
constexpr const char* MAIN = "/.crosspoint/reading-stats.json";
constexpr const char* RESET = "/.crosspoint/reading-stats.reset";
unsigned checks = 0, failures = 0;

void check(bool ok, const std::string& message) {
  ++checks;
  if (!ok) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

std::string bytes(const std::string& path) {
  std::ifstream input(fixtureRoot + path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::map<std::string, std::string> bookBytes() {
  std::map<std::string, std::string> files;
  const auto root = std::filesystem::path(fixtureRoot + "/.crosspoint/reading-stats");
  if (!std::filesystem::exists(root)) return files;
  for (const auto& item : std::filesystem::directory_iterator(root)) {
    if (item.is_regular_file()) files[item.path().filename().string()] = bytes("/.crosspoint/reading-stats/" + item.path().filename().string());
  }
  return files;
}

const std::string oldMain =
    "{\"schema\":4,\"bookEpoch\":7,\"habits\":{\"awarded\":3},\"ngay\":[[20260920,12,4]],"
    "\"activeBook\":{\"path\":\"/Books/current.epub\",\"title\":\"Current\",\"minutes\":5}}";
const std::string oldBackup = "{\"schema\":4,\"bookEpoch\":7,\"ngay\":[[20260919,8,2]]}";

void seed() {
  readFault = {};
  storageFault = {};
  fixture(2);
  check(Storage.writeFile(MAIN, oldMain), "seed main");
  check(Storage.writeFile((std::string(MAIN) + ".bak").c_str(), oldBackup), "seed backup");
  JsonDocument doc;
  check(!deserializeJson(doc, oldMain), "parse seed memory");
  check(READING_STATS.fromJson(doc.as<JsonVariantConst>()), "seed memory");
  READING_STATS.statisticsReadable = true;
}

using ResetResult = ReadingStatsStore::ResetResult;
static_assert(std::is_same_v<decltype(READING_STATS.resetStatistics(false)), ResetResult>);

void expectOldCommittedBytes(const std::map<std::string, std::string>& books, const std::string& label) {
  check(bytes(MAIN) == oldMain, label + " keeps main bytes");
  check(bytes(std::string(MAIN) + ".bak") == oldBackup, label + " keeps backup bytes");
  check(bookBytes() == books, label + " keeps book bytes");
  check(!Storage.exists(RESET), label + " has no committed journal");
}

void precommit(StorageFaultOperation operation, bool all, const std::string& label) {
  seed();
  const auto books = bookBytes();
  storageFault.operation = operation;
  storageFault.path = std::string(RESET) + ".tmp";
  if (operation == StorageFaultOperation::Rename) storageFault.target = RESET;
  check(READING_STATS.resetStatistics(all) == ResetResult::Failed, label + " returns Failed");
  check(storageFault.hits == 1, label + " hits exact injected operation");
  expectOldCommittedBytes(books, label);
}

void finalization(StorageFaultOperation operation, const std::string& path, const std::string& target, bool all,
                  const std::string& label) {
  seed();
  const auto books = bookBytes();
  storageFault = {operation, path, target, 0};
  check(READING_STATS.resetStatistics(all) == ResetResult::Pending, label + " returns Pending");
  check(storageFault.hits == 1, label + " hits exact injected operation");
  check(Storage.exists(RESET), label + " keeps committed journal");
  const std::string journal = bytes(RESET);
  check(!journal.empty(), label + " journal has bytes");
  JsonDocument pending;
  check(!deserializeJson(pending, journal) && pending.is<JsonObject>(), label + " journal is valid object");
  check(bookBytes() == books, label + " keeps books while pending");
  check(!READING_STATS.saveToFile(), label + " blocks save while journal is pending");
  check(READING_STATS.resetStatistics(all) == ResetResult::Pending,
        label + " repeated reset reports existing pending journal");
  check(bytes(RESET) == journal && bookBytes() == books, label + " repeated operations retain pending bytes");
  if (operation == StorageFaultOperation::Remove && path == std::string(MAIN) + ".bak") {
    check(bytes(MAIN) == oldMain && bytes(std::string(MAIN) + ".bak") == oldBackup,
          label + " keeps main and backup before first finalization mutation");
  } else if (operation == StorageFaultOperation::Remove) {
    check(bytes(MAIN) == oldMain && !Storage.exists((std::string(MAIN) + ".bak").c_str()),
          label + " keeps main after backup removal");
  } else {
    check(!Storage.exists(MAIN) && !Storage.exists((std::string(MAIN) + ".bak").c_str()),
          label + " reaches final rename after old copies removed");
  }

  storageFault = {};
  check(READING_STATS.loadFromFile(), label + " clean reload replays journal");
  check(!Storage.exists(RESET), label + " clean reload consumes journal");
  check(bytes(MAIN) == journal, label + " clean reload promotes exact journal bytes");
  check(all ? bookBytes().empty() : bookBytes() == books, label + " cleanup matches reset scope after finalization");
}

void healthy(bool all, const std::string& label) {
  seed();
  const auto books = bookBytes();
  check(READING_STATS.resetStatistics(all) == ResetResult::Complete, label + " returns Complete");
  check(!Storage.exists(RESET), label + " leaves no journal");
  check(all ? bookBytes().empty() : bookBytes() == books, label + " cleanup matches completed scope");
}

void retainedJournalFailure(bool unavailable) {
  seed();
  check(Storage.writeFile(RESET, "{corrupt reset journal"), "seed corrupt journal");
  check(Storage.writeFile((std::string(RESET) + ".davbak").c_str(),
                          "{\"schema\":4,\"bookEpoch\":99}"),
        "seed stale reset davbak");
  check(Storage.writeFile((std::string(RESET) + ".bak").c_str(),
                          "{\"schema\":4,\"bookEpoch\":98}"),
        "seed stale reset backup");
  const auto books = bookBytes();
  const auto main = bytes(MAIN), backup = bytes(std::string(MAIN) + ".bak"), journal = bytes(RESET);
  const auto journalDavbak = bytes(std::string(RESET) + ".davbak");
  const auto journalBackup = bytes(std::string(RESET) + ".bak");
  if (unavailable) {
    readFault = {};
    readFault.path = RESET;
    readFault.fullPath = true;
    readFault.openFails = true;
  }
  const std::string label = unavailable ? "unavailable journal" : "corrupt journal";
  check(!READING_STATS.loadFromFile(), label + " fails closed");
  check(!unavailable || readFault.hits > 0, label + " fault hits exact journal");
  check(!READING_STATS.statisticsReadable, label + " marks store unreadable");
  check(bytes(MAIN) == main && bytes(std::string(MAIN) + ".bak") == backup && bytes(RESET) == journal,
        label + " retains global bytes");
  check(bytes(std::string(RESET) + ".davbak") == journalDavbak &&
            bytes(std::string(RESET) + ".bak") == journalBackup,
        label + " ignores and retains journal recovery artifacts");
  check(bookBytes() == books, label + " retains book bytes");
  check(READING_STATS.resetStatistics(false) == ResetResult::Failed &&
            READING_STATS.resetStatistics(true) == ResetResult::Failed,
        label + " refuses both reset scopes");
}
}  // namespace

int main() {
  for (const bool all : {false, true}) {
    const std::string scope = all ? "all" : "habits";
    precommit(StorageFaultOperation::OpenWrite, all, scope + " precommit open");
    precommit(StorageFaultOperation::Write, all, scope + " precommit write");
    precommit(StorageFaultOperation::CloseWrite, all, scope + " precommit close");
    precommit(StorageFaultOperation::Rename, all, scope + " precommit rename");
    finalization(StorageFaultOperation::Remove, std::string(MAIN) + ".bak", "", all,
                 scope + " backup remove");
    finalization(StorageFaultOperation::Remove, MAIN, "", all, scope + " main remove");
    finalization(StorageFaultOperation::Rename, RESET, MAIN, all, scope + " final rename");
    healthy(all, scope + " healthy finalization");
  }
  retainedJournalFailure(false);
  retainedJournalFailure(true);
  std::filesystem::remove_all(fixtureRoot);
  std::cout << "RESULT " << checks - failures << '/' << checks << " assertions passed\n";
  return failures ? 1 : 0;
}
