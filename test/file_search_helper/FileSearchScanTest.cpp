#include "FileSearchScan.h"
#include <cstdlib>
#include <iostream>
#include <map>
#include <vector>

namespace {
void require(bool ok, const char* message) { if (!ok) { std::cerr << message << '\n'; std::exit(1); } }
struct Clock final : filesearch::ScanHooks {
  uint32_t now = 0;
  bool stop = false, hidden = false, failAlloc = false;
  int locks = 0, unlocks = 0;
  size_t bytes = 0;
  uint32_t nowMs() const override { return now; }
  bool cancelled() const override { return stop; }
  bool showHidden() const override { return hidden; }
  void lockSlice() override { ++locks; }
  void unlockSlice() override { ++unlocks; }
  filesearch::ScanBuffer allocate(size_t n) override {
    bytes = n;
    return failAlloc ? filesearch::ScanBuffer(nullptr, [](uint8_t*) {}) : ScanHooks::allocate(n);
  }
};
struct Dir final : filesearch::Directory {
  Dir(const std::vector<filesearch::ScanEntry>& values, Clock& clock, uint32_t delay, int cancel)
      : entries(values), clock(clock), delay(delay), cancelAt(cancel) {}
  bool next(filesearch::ScanEntry& out) override {
    clock.now += delay;
    if (pos == entries.size()) return false;
    out = entries[pos++];
    if (int(pos) == cancelAt) clock.stop = true;
    return true;
  }
  const std::vector<filesearch::ScanEntry>& entries;
  Clock& clock;
  uint32_t delay;
  int cancelAt;
  size_t pos = 0;
};
struct Store final : filesearch::SearchStorage {
  explicit Store(Clock& clock) : clock(clock) {}
  Clock& clock;
  std::map<std::string, std::vector<filesearch::ScanEntry>> tree;
  uint32_t delay = 0;
  int cancelAt = -1, failOpen = -1, opens = 0;
  std::unique_ptr<filesearch::Directory> open(const std::string& path) override {
    require(clock.locks == clock.unlocks + 1, "every open is power locked");
    if (++opens == failOpen) return {};
    auto it = tree.find(path);
    if (it == tree.end()) return {};
    return std::make_unique<Dir>(it->second, clock, delay, cancelAt);
  }
};
void finish(filesearch::Scanner& s) { while (s.tick() == filesearch::ScanReason::Running) {} }
}
int main() {
  Clock clock;
  Store store(clock);
  store.tree["/"] = {{"z", true}, {"a", true}, {".hidden.epub", false, true}, {"System Volume Information", true}};
  store.tree["/z"] = {{"Book 2.epub", false}, {"Đường 2.epub", false}};
  store.tree["/a"] = {{"Book 10.epub", false}, {"Book 2.epub", false}};
  filesearch::Scanner s;
  require(s.start(store, clock, "/", "book"), "start");
  finish(s);
  require(s.reason() == filesearch::ScanReason::Complete, "complete");
  require(s.resultCount() == 3 && std::string(s.result(0).path) == "/a/Book 2.epub" &&
          std::string(s.result(1).path) == "/z/Book 2.epub", "basename order then raw path");
  require(clock.bytes == 67584 && clock.locks == clock.unlocks, "bounded bytes and balanced locks");
  require(s.start(store, clock, "/", "DUONG"), "raw path start"); finish(s);
  require(s.resultCount() == 1 && std::string(s.result(0).path) == "/z/Đường 2.epub", "preserve NFD for IO");
  clock.hidden = true;
  require(s.start(store, clock, "/", "hidden"), "hidden start"); finish(s);
  require(s.resultCount() == 1 && s.reason() == filesearch::ScanReason::Complete, "hidden setting and Windows directory");
  clock.hidden = false;

  Store deep(clock);
  std::string dir = "/";
  for (int depth = 0; depth < 7; ++depth) {
    deep.tree[dir] = {{"Book.epub", false}, {"d", true}};
    dir += (dir == "/" ? "" : "/") + std::string("d");
  }
  require(s.start(deep, clock, "/", "book"), "depth start"); finish(s);
  require(s.stats().depth == 5 && s.resultCount() == 6 && s.reason() == filesearch::ScanReason::Limit, "depth 5 included 6 excluded and incomplete");

  Store large(clock);
  for (int i = 8192; i >= 0; --i) large.tree["/"].push_back({"Book " + std::to_string(i) + ".epub", false});
  require(s.start(large, clock, "/", "book"), "large start");
  while (s.reason() == filesearch::ScanReason::Running) {
    const auto before = s.stats().entries; s.tick();
    require(s.stats().entries - before <= 32, "32 entries per slice");
  }
  require(s.stats().entries == 8192 && s.reason() == filesearch::ScanReason::Limit && s.resultCount() == 256, "8193 entries stop at 8192 and top 256");
  require(std::string(s.result(0).path) == "/Book 1.epub" && std::string(s.result(255).path) == "/Book 256.epub", "top 256 survive pool compaction");
  large.tree["/"].pop_back();
  require(s.start(large, clock, "/", "unmatched"), "8192 start"); finish(s);
  require(s.stats().entries == 8192 && s.resultCount() == 0, "8192 exact visit ceiling");

  Store slow(clock);
  for (int i = 0; i < 100; ++i) slow.tree["/"].push_back({"ignored.pdf", false});
  slow.delay = 11;
  require(s.start(slow, clock, "/", "book"), "ignored time start"); s.tick();
  require(s.stats().entries == 2 && s.stats().sliceMs == 22, "time boundary covers ignored files");
  slow.delay = 75;
  require(s.start(slow, clock, "/", "book"), "blocked card start"); s.tick();
  require(s.stats().entries == 1 && s.stats().maxBlockMs == 75, "single slow call yields immediately");
  finish(s);
  require(s.reason() == filesearch::ScanReason::Timeout, "2 second total budget");
  slow.cancelAt = 1;
  clock.stop = false;
  require(s.start(slow, clock, "/", "book"), "cancel during read start"); s.tick();
  require(s.reason() == filesearch::ScanReason::Cancelled && s.stats().readCalls == 2, "cancel checked after card call");
  clock.stop = false;
  for (int batch = 0; batch < 8; ++batch) {
    require(s.start(large, clock, "/", "book"), "batch cancel start");
    for (int i = 0; i < batch; ++i) s.tick();
    const auto calls = s.stats().readCalls;
    s.cancel(); s.tick();
    require(s.reason() == filesearch::ScanReason::Cancelled && s.stats().readCalls == calls && s.stats().cancelMs == 0, "cancel every batch without IO");
  }
  clock.failAlloc = true;
  require(!s.start(store, clock, "/", "book") && s.resultCount() == 0, "allocation refusal clean");
  clock.failAlloc = false;
  filesearch::ScanLimits tiny; tiny.maxPathBytes = 4;
  require(s.start(store, clock, "/", "book", tiny), "tiny pool start"); finish(s);
  require(s.reason() == filesearch::ScanReason::Oom && s.resultCount() == 0, "path pool refusal clean");
  for (int fail = 1; fail <= 3; ++fail) {
    store.opens = 0; store.failOpen = fail;
    require(s.start(store, clock, "/", "book"), "directory refusal start"); finish(s);
    require(s.reason() == filesearch::ScanReason::Error, "directory refusal clean");
  }
  require(clock.locks == clock.unlocks, "all exits release power lock");
  std::cout << "scan PASS: alloc=67584 top=256 entries=8192 slice_entries=32 slow_block_ms=75 cancel_ms=0\n";
}
