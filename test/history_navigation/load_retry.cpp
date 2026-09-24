// A statistics file the boot could not read must not cost the whole reading
// session. On the X3 every wake is a fresh boot, so one failed read at boot
// used to leave the store unreadable until the next sleep: the book opened,
// activateBook() refused, and nothing read in that wake was ever recorded.
#define STATS_SCHEMA_HELPERS_ONLY
#include "schema_preservation.cpp"
#include "ReadingStatsStore.cpp"

namespace {
void failedBootReadRecoversWhenABookOpens() {
  readFault = {};
  seed();
  const std::string path = ReadingStatsStore::getFilePath();
  check(Storage.writeFile(path.c_str(), document("4")), "seed statistics");
  readFault.path = path;
  readFault.fullPath = true;
  readFault.openFails = true;
  check(!READING_STATS.loadFromFile(), "boot read fails");
  check(!READING_STATS.statisticsReadable, "store unreadable after the failed boot read");
  readFault = {};
  check(READING_STATS.activateBook("/sach.epub", 5, "Sach"), "opening a book retries the read");
  check(READING_STATS.statisticsReadable, "store readable after the retry");
  READING_STATS.record(20260924, 90000, 3, 6);
  check(READING_STATS.saveToFile(), "reading after the retry is saved");
  JsonDocument saved;
  check(!deserializeJson(saved, Storage.readFile(path.c_str())), "saved file parses");
  check(saved["ngay"][0][0].as<unsigned>() == 20260913 && saved["ngay"][0][1].as<unsigned>() == 12,
        "earlier days kept");
  check(saved["ngay"][1][0].as<unsigned>() == 20260924 && saved["ngay"][1][2].as<unsigned>() == 3,
        "new reading recorded");
}

void unreadableFileStillRefuses() {
  readFault = {};
  seed();
  const std::string path = ReadingStatsStore::getFilePath();
  check(Storage.writeFile(path.c_str(), "{broken"), "seed corrupt main");
  check(Storage.writeFile((path + ".bak").c_str(), "{broken"), "seed corrupt backup");
  const auto original = snapshot();
  check(!READING_STATS.loadFromFile(), "corrupt statistics refuse");
  check(!READING_STATS.activateBook("/sach.epub", 5, "Sach"), "a book open does not write over them");
  check(!READING_STATS.saveToFile(), "save still refuses");
  check(snapshot() == original, "corrupt files kept byte for byte");
}
}  // namespace

int main() {
  failedBootReadRecoversWhenABookOpens();
  unreadableFileStillRefuses();
  std::filesystem::remove_all(fixtureRoot);
  std::cout << "RESULT " << checks - failures << '/' << checks << " assertions passed\n";
  return failures ? 1 : 0;
}
