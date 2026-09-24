// N6: writeSnapshot() kept the previous main file as the backup only after reading it whole (up to
// 32 KB from the card) to learn it was readable. A file this boot has itself read whole or written
// is known readable, so the write goes without that read: the shared ledger after load or its
// last save, the active book's file after activation.
#define STATS_SCHEMA_HELPERS_ONLY
#include "schema_preservation.cpp"
#include "ReadingStatsStore.cpp"

int main() {
  seed();
  const std::string main = ReadingStatsStore::getFilePath();
  check(Storage.writeFile(main.c_str(), document("4")), "seed ledger");
  check(READING_STATS.loadFromFile(), "load ledger");
  io = {};
  check(READING_STATS.saveToFile(), "save after load");
  check(io.parses == 0, "save after load parses the ledger again: " + std::to_string(io.parses));
  io = {};
  check(READING_STATS.saveToFile(), "second save");
  check(io.parses == 0, "second save parses the ledger again: " + std::to_string(io.parses));
  // The previous snapshot is still the backup, and both read back.
  JsonDocument doc;
  check(PersistableStoreBase::readDocFromFileStatus((main + ".bak").c_str(), doc, 32768) ==
            PersistableStoreBase::ReadResult::Ready,
        "backup readable");
  check(READING_STATS.loadFromFile(), "reload");

  // Books: A active, then B (A archived), then A again (its file read), then C: A's file is written
  // over without being read first.
  check(READING_STATS.activateBook("/Books/a.epub", 10, "A"), "activate A");
  check(READING_STATS.activateBook("/Books/b.epub", 20, "B"), "activate B");
  check(READING_STATS.activateBook("/Books/a.epub", 10, "A"), "activate A again");
  io = {};
  check(READING_STATS.activateBook("/Books/c.epub", 30, "C"), "activate C");
  check(io.parses == 0, "archiving the active book parses its file first: " + std::to_string(io.parses));
  check(Storage.exists((bookFile("/Books/a.epub") + ".bak").c_str()), "A's previous file kept as backup");
  BookReadingRecord a;
  check(READING_STATS.readBook("/Books/a.epub", a) && a.startProgress == 10, "A reads back");

  // A snapshot the store has not read or written this boot is still read before it is replaced.
  io = {};
  JsonDocument next;
  next["schema"] = 4;
  check(writeSnapshot(bookFile("/Books/b.epub"), next), "direct write");
  check(io.parses == 1, "an unknown file is read first: " + std::to_string(io.parses));
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
