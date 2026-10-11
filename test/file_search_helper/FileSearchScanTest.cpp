#include "FileSearchScan.h"

#include <cstdlib>
#include <iostream>
#include <map>
#include <vector>

namespace {
struct Clock final : filesearch::ScanHooks {
  uint32_t now = 0;
  bool stop = false;
  uint32_t nowMs() const override { return now; }
  bool cancelled() const override { return stop; }
  void lockSlice() override { now += 1; }
  void unlockSlice() override { now += 1; }
};
struct Dir final : filesearch::Directory {
  explicit Dir(std::vector<filesearch::ScanEntry> values) : entries(std::move(values)) {}
  bool next(filesearch::ScanEntry& out) override {
    if (pos == entries.size()) return false;
    out = entries[pos++];
    return true;
  }
  std::vector<filesearch::ScanEntry> entries;
  size_t pos = 0;
};
struct Store final : filesearch::Storage {
  std::map<std::string, std::vector<filesearch::ScanEntry>> tree;
  std::unique_ptr<filesearch::Directory> open(const std::string& path) override {
    auto it = tree.find(path);
    if (it == tree.end()) return {};
    return std::make_unique<Dir>(it->second);
  }
};
void require(bool ok, const char* message) { if (!ok) { std::cerr << message << '\n'; std::exit(1); } }
}

int main() {
  Store store;
  store.tree["/"] = {{"a", true}, {"hidden", false, true}, {"root.epub", false}};
  store.tree["/a"] = {{"b", true}, {"Duong.epub", false}};
  store.tree["/a/b"] = {{"Duong 2.epub", false}};
  Clock clock;
  filesearch::Scanner scanner;
  require(scanner.start(store, clock, "/", "duong"), "scanner start");
  while (scanner.tick() == filesearch::ScanReason::Running) {}
  require(scanner.reason() == filesearch::ScanReason::Complete, "scanner complete");
  require(scanner.resultCount() == 2, "nested results");
  require(std::string(scanner.result(0).path) == "/a/Duong.epub", "natural result path");
  require(scanner.stats().entries == 6, "all directory entries counted");

  filesearch::ScanLimits limit;
  limit.maxEntries = 2;
  require(scanner.start(store, clock, "/", "duong", limit), "limit start");
  while (scanner.tick() == filesearch::ScanReason::Running) {}
  require(scanner.reason() == filesearch::ScanReason::Limit, "entry limit");

  require(scanner.start(store, clock, "/", "duong"), "cancel start");
  clock.stop = true;
  require(scanner.tick() == filesearch::ScanReason::Cancelled, "cancel between entries");

  Store large;
  clock.stop = false;
  large.tree["/"].reserve(8193);
  for (int i = 0; i < 8193; ++i) large.tree["/"].push_back({"Book" + std::to_string(i) + ".epub", false});
  filesearch::ScanLimits largeLimit;
  largeLimit.maxEntries = 8192;
  largeLimit.maxPathBytes = 256 * 1024;
  require(scanner.start(large, clock, "/", "book", largeLimit), "large start");
  while (scanner.tick() == filesearch::ScanReason::Running) {}
  if (!(scanner.reason() == filesearch::ScanReason::Limit && scanner.stats().entries == 8192)) {
    std::cerr << "large reason=" << static_cast<int>(scanner.reason()) << " entries=" << scanner.stats().entries << '\n';
    require(false, "8192 entry ceiling");
  }

  filesearch::ScanLimits oomLimit;
  oomLimit.maxPathBytes = 4;
  require(scanner.start(store, clock, "/", "duong", oomLimit), "path pool start");
  while (scanner.tick() == filesearch::ScanReason::Running) {}
  require(scanner.reason() == filesearch::ScanReason::Oom, "path pool refuses cleanly");
  std::cout << "file search scan: PASS\n";
}
