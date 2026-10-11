// The idle cover step (coverink::build) on a card in RAM, with the real decoder and tenor/ink kernel.
#include <HalStorage.h>
#include <HeapFloor.h>
#include <RecoverableFile.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <vector>

#include "CoverInk.h"
#include "TenorInk.h"

EspClass ESP;

namespace {
size_t live = 0;
const char* refuseWho = nullptr;

std::vector<uint8_t> load(const char* name) {
  std::ifstream in(std::string(FIXTURES) + "/" + name, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}

struct Bytes : Print {
  std::vector<uint8_t> b;
  size_t write(uint8_t v) override { b.push_back(v); return 1; }
  size_t write(const uint8_t* p, size_t n) override { b.insert(b.end(), p, p + n); return n; }
};

std::vector<uint8_t> coverOf(const uint8_t* gray, int sw, int sh, int w, int h, bool four) {
  Bytes out;
  assert(tenorink::writeCover(gray, sw, sh, out, w, h, four));
  return out.b;
}

// The cover in the book: streamed in pieces, a stop asked before each, as the device's zip copy does.
enum class Stage { None, Sniff, Copy, Decode };
struct Book {
  std::vector<uint8_t> jpeg;
  bool notJpeg = false;
  progluma::Result copyFails = progluma::Result::Done;
  int sniffs = 0, copies = 0, releases = 0;
  Stage stage = Stage::None;
};
progluma::Result sniff(void* user, progluma::Sniffer& s, const idlework::Stop& stop) {
  auto& b = *static_cast<Book*>(user);
  ++b.sniffs;
  b.stage = Stage::Sniff;
  if (b.notJpeg) return progluma::Result::Unsupported;
  for (size_t at = 0; at < b.jpeg.size(); at += 64) {
    if (stop()) return progluma::Result::Stopped;
    if (s.feed(b.jpeg.data() + at, std::min<size_t>(64, b.jpeg.size() - at))) break;
  }
  return progluma::Result::Done;
}
progluma::Result copy(void* user, const std::string& path, const idlework::Stop& stop) {
  auto& b = *static_cast<Book*>(user);
  ++b.copies;
  b.stage = Stage::Copy;
  if (b.copyFails != progluma::Result::Done) return b.copyFails;
  HalFile f;
  Storage.openFileForWrite("T", path, f);
  for (size_t at = 0; at < b.jpeg.size(); at += 512) {
    if (stop()) {
      f.close();
      return progluma::Result::Stopped;
    }
    const size_t n = std::min<size_t>(512, b.jpeg.size() - at);
    if (f.write(b.jpeg.data() + at, n) != n) {
      f.close();
      return progluma::Result::Io;
    }
  }
  b.stage = Stage::Decode;
  return f.close() ? progluma::Result::Done : progluma::Result::Io;
}

// Keys and a clock the cases set.
struct Keys {
  Book* book = nullptr;
  Stage at = Stage::None;
  int asks = 0, yesAt = 0;  // yesAt: the key is down at that question, wherever it falls
} keys;
uint32_t nowMs = 0, tickMs = 0;
const char* lastWhy = "";
uint32_t clockMs() { return nowMs += tickMs; }
idlework::Stop stopper() {
  idlework::Stop s;
  s.keyDown = [](void* u) {
    auto& k = *static_cast<Keys*>(u);
    ++k.asks;
    if (k.yesAt) return k.asks == k.yesAt;
    return k.at != Stage::None && k.book && k.book->stage == k.at;
  };
  s.user = &keys;
  s.clockMs = clockMs;
  s.startedMs = nowMs;
  return s;
}

const std::string dir = "/.crosspoint/epub_7";
const std::string state = dir + "/cover.ink";
std::string ink(int w, int h, bool four) {
  return tenorink::cachePath(dir + "/thumb_[HEIGHT].bmp", w, h, four);
}
std::vector<uint8_t> blurred(int w, int h, bool four) {
  std::vector<uint8_t> flat(64 * 96, 128);
  return coverOf(flat.data(), 64, 96, w, h, four);
}
const char* cardTails[] = {".carde", ".card0", ".card1", ".card2", ".card3", ".carde.davbak", ".card2.davtmp"};

void fresh(int w = 96, int h = 140, bool four = true) {
  Storage.files.clear();
  Storage.capacity = SIZE_MAX;
  Storage.cutAt = Storage.changes = Storage.readFails = 0;
  Storage.watchPath = dir + "/.cover.jpg";
  Storage.watchReads = Storage.watchFailAt = 0;
  ESP.freeHeap = ESP.largestHeap = 200000;
  Storage.files[ink(w, h, four)] = blurred(w, h, four);
  for (const char* t : cardTails) Storage.files[ink(w, h, four) + t] = {1, 2, 3};
  keys = {};
  tickMs = 0;
  refuseWho = nullptr;
}

coverink::Outcome step(Book& book, int w = 96, int h = 140, bool four = true, std::string path = {}) {
  if (path.empty()) path = ink(w, h, four);
  keys.book = &book;
  book.stage = Stage::None;
  coverink::Source src;
  src.sniff = sniff;
  src.copy = copy;
  src.release = [](void* u) { ++static_cast<Book*>(u)->releases; };
  src.user = &book;
  const size_t before = live;
  lastWhy = "";
  const auto out = coverink::build(dir, path, w, h, four, src, stopper(), &lastWhy);
  assert(live == before);  // every block the step took is back
  return out;
}

coverink::Record record() {
  coverink::Record r;
  coverink::read(state, r);
  return r;
}
coverink::Status status(int w = 96, int h = 140, bool four = true) {
  const auto r = record();
  const auto* f = r.find(w, h, four);
  return f ? f->status : coverink::Status::Pending;
}
int failures(int w = 96, int h = 140, bool four = true) {
  const auto r = record();
  const auto* f = r.find(w, h, four);
  return f ? f->failures : 0;
}
bool cardsGone(int w = 96, int h = 140, bool four = true) {
  for (const char* t : cardTails)
    if (Storage.exists((ink(w, h, four) + t).c_str())) return false;
  return true;
}
bool coverCopy() { return Storage.exists((dir + "/.cover.jpg").c_str()); }
bool leftovers() {
  for (const auto& f : Storage.files)
    if (f.first.find(".davtmp") != std::string::npos || f.first.find(".davbak") != std::string::npos ||
        f.first.find(".cover.jpg") != std::string::npos)
      return true;
  return false;
}
}  // namespace

namespace heapfloor {
bool judge(size_t, const void*, const char* who) { return !refuseWho || std::strcmp(who, refuseWho) != 0; }
void* take(size_t bytes, const char*) { return std::malloc(bytes); }
}  // namespace heapfloor

// The nothrow allocations are the firmware's blocks (makeUniqueNoThrow, heapfloor::array): only
// those count; the test's own containers use the plain forms.
void* allocate(size_t n, bool counted) {
  auto* p = static_cast<size_t*>(std::malloc(n + 16));
  if (!p) return nullptr;
  p[0] = counted ? n : 0;
  live += p[0];
  return p + 2;
}
void* operator new(size_t n, const std::nothrow_t&) noexcept { return allocate(n, true); }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { return allocate(n, true); }
void* operator new(size_t n) {
  void* p = allocate(n, false);
  if (!p) std::abort();
  return p;
}
void* operator new[](size_t n) { return operator new(n); }
void operator delete(void* p) noexcept {
  if (!p) return;
  auto* head = static_cast<size_t*>(p) - 2;
  live -= head[0];
  std::free(head);
}
void operator delete[](void* p) noexcept { operator delete(p); }
void operator delete(void* p, size_t) noexcept { operator delete(p); }
void operator delete[](void* p, size_t) noexcept { operator delete(p); }

int main() {
  using O = coverink::Outcome;
  using S = coverink::Status;
  const auto jpeg = load("cover.jpg"), pgm = load("cover.pgm");
  // The libjpeg luma (Pillow) after its 3 header lines, through the same kernel: the cover expected.
  size_t at = 0;
  for (int lines = 0; lines < 3; ++at) lines += pgm[at] == '\n';
  const auto sharp = coverOf(pgm.data() + at, 120, 181, 96, 140, true);

  // P to S: the sharp cover replaces the temporary one, the cards drawn from it go, S is kept.
  fresh();
  Book book{jpeg};
  assert(step(book) == O::Sharp);
  assert(Storage.files[ink(96, 140, true)] == sharp);
  assert(record().kind == coverink::Kind::Progressive && status() == S::Sharp);
  assert(record().width == 120 && record().height == 181);
  assert(cardsGone() && !leftovers() && book.releases == 1);
  const int asksWhole = keys.asks, readsWhole = Storage.watchReads;
  assert(step(book) == O::Nothing && book.sniffs == 1 && book.copies == 1 && book.releases == 1);
  assert(std::strcmp(lastWhy, "sharp") == 0);
  std::puts("GREEN progressive cover: sharp file equals libjpeg luma through tenor/ink, cards gone, S kept, not again");
  std::puts("GREEN the card's snapshot is asked back once, before the step's memory; a step with nothing to do keeps it");

  // The X3's own case: a cover its JPEGDEC cannot decode (DC split by component, err=2) never got a
  // temporary cover. The step writes the sharp one in its place all the same.
  {
    const auto split = load("dcsplit.jpg"), splitPgm = load("dcsplit.pgm");
    // CoverInkJpegdecTest shows the X3's JPEGDEC path failing on this file.
    fresh();
    Storage.files.erase(ink(96, 140, false));
    Book b{split};
    assert(step(b, 96, 140, false) == O::Sharp);
    size_t from = 0;
    for (int lines = 0; lines < 3; ++from) lines += splitPgm[from] == '\n';
    assert(Storage.files[ink(96, 140, false)] == coverOf(splitPgm.data() + from, 120, 181, 96, 140, false));
    assert(step(b, 96, 140, false) == O::Nothing && std::strcmp(lastWhy, "sharp") == 0);
  }
  std::puts("GREEN a cover JPEGDEC cannot decode, with no temporary cover: the sharp 1 bit cover is written");

  // 2 depths x 3 text sizes: 6 entries, each its own, none built twice, none pushed out.
  const int sizes[3][2] = {{96, 140}, {80, 120}, {70, 104}};
  fresh();
  for (int d = 0; d < 2; ++d)
    for (const auto& s : sizes)
      if (!(d == 1 && s[0] == 96)) Storage.files[ink(s[0], s[1], d == 1)] = blurred(s[0], s[1], d == 1);
  {
    Book b{jpeg};
    for (int d = 0; d < 2; ++d)
      for (const auto& s : sizes) assert(step(b, s[0], s[1], d == 1) == O::Sharp);
    for (int d = 0; d < 2; ++d)
      for (const auto& s : sizes) assert(step(b, s[0], s[1], d == 1) == O::Nothing);
    assert(b.copies == 6 && record().count == 6);
    assert(Storage.files[ink(96, 140, false)] == coverOf(pgm.data() + at, 120, 181, 96, 140, false));
  }
  std::puts("GREEN 1 bit and 4 levels at 3 frame sizes: 6 entries kept, none built twice");

  // Only a tenor/ink cover of this book's cache: anything else is left alone, nothing read.
  {
    fresh();
    Book b{jpeg};
    Storage.files[dir + "/thumb2_450.bmp"] = blurred(96, 140, true);
    Storage.files["/.crosspoint/epub_8/ink2_4_96x140.bmp"] = blurred(96, 140, true);
    assert(step(b, 96, 140, true, dir + "/thumb2_450.bmp") == O::Nothing);
    assert(step(b, 96, 140, true, "/.crosspoint/epub_8/ink2_4_96x140.bmp") == O::Nothing);
    assert(step(b, 96, 140, true, dir + "/sub/ink2_4_96x140.bmp") == O::Nothing);
    assert(b.sniffs == 0 && b.copies == 0 && b.releases == 0);
  }
  std::puts("GREEN a path outside the book's cache or not a tenor/ink cover: nothing done");

  // Baseline and not a JPEG: B, never again; CMYK: F, never again; the temporary cover stays.
  for (const char* name : {"baseline.jpg", "png"}) {
    fresh();
    Book b{name[0] == 'p' ? std::vector<uint8_t>{} : load(name)};
    b.notJpeg = name[0] == 'p';
    const auto before = Storage.files[ink(96, 140, true)];
    assert(step(b) == O::Nothing && record().kind == coverink::Kind::Baseline);
    assert(step(b) == O::Nothing && b.sniffs == 1 && b.copies == 0 && Storage.files[ink(96, 140, true)] == before);
  }
  {
    fresh();
    Book b{load("cmyk.jpg")};
    const auto before = Storage.files[ink(96, 140, true)];
    assert(step(b) == O::Gone && record().kind == coverink::Kind::Failed);
    assert(step(b) == O::Nothing && b.sniffs == 1 && Storage.files[ink(96, 140, true)] == before);
  }
  std::puts("GREEN baseline and PNG mark B, CMYK marks F; neither is tried again, the temporary cover stays");

  // A frame the kernel cannot take: given up at once, nothing read.
  {
    fresh();
    Storage.files[ink(900, 140, true)] = {'B', 'M'};  // a cover drawn some other way
    Book b{jpeg};
    assert(step(b, 900, 140, true) == O::Failed && status(900, 140, true) == S::GaveUp);
    assert(b.sniffs == 0 && b.copies == 0 && step(b, 900, 140, true) == O::Nothing);
  }
  std::puts("GREEN a frame too large for the kernel is given up for good, not tried for heap");

  // A key at each of the 3 stages: nothing is marked, the cover is untouched, no copy left; then sharp.
  for (const Stage s : {Stage::Sniff, Stage::Copy, Stage::Decode}) {
    fresh();
    Book b{jpeg};
    const auto before = Storage.files[ink(96, 140, true)];
    keys.at = s;
    assert(step(b) == O::Stopped && !coverCopy());
    assert(Storage.files[ink(96, 140, true)] == before && status() == S::Pending && failures() == 0);
    // At the decode stage the first question is asked before the cover's first sector is read.
    if (s == Stage::Decode) assert(Storage.watchReads == 0);
    keys.at = Stage::None;
    assert(step(b) == O::Sharp && Storage.files[ink(96, 140, true)] == sharp);
  }
  std::puts("GREEN a key while reading the book, copying the cover or opening it: stopped, nothing read past it, then sharp");

  // A key at the last 2 questions (before the whole-file check, before the replacement): stopped,
  // the old cover stays; after the replacement nothing asks, so cover and mark agree.
  for (int back = 0; back < 2; ++back) {
    fresh();
    Book b{jpeg};
    keys.yesAt = asksWhole - back;
    const auto before = Storage.files[ink(96, 140, true)];
    assert(step(b) == O::Stopped && Storage.files[ink(96, 140, true)] == before && status() == S::Pending);
    // Heard after the decode: the sharp file is whole in staging, only not put in place.
    assert(!coverCopy() && Storage.files[ink(96, 140, true) + ".davtmp"] == sharp);
    keys.yesAt = 0;
    assert(step(b) == O::Sharp);
  }
  std::puts("GREEN a key at the end of the step, before the replacement: stopped, old cover kept");

  // The step's own time: counted, the third gives the frame up; the other depth still builds.
  fresh();
  Storage.files[ink(96, 140, false)] = blurred(96, 140, false);
  {
    Book b{jpeg};
    tickMs = 1000;
    for (int i = 1; i <= 3; ++i) assert(step(b) == O::Failed && failures() == i && !coverCopy());
    assert(status() == S::GaveUp && step(b) == O::Nothing);
    tickMs = 0;
    assert(step(b, 96, 140, false) == O::Sharp);
  }
  std::puts("GREEN a step past its time cap counts; the third gives the frame up; the other depth is not held");

  // A full card while the sharp file is written: the temporary cover stays, counted, nothing left over.
  fresh();
  {
    Book b{jpeg};
    Storage.capacity = Storage.used() + jpeg.size() + 2 * 60 + 1500;
    const auto before = Storage.files[ink(96, 140, true)];
    assert(step(b) == O::Failed && failures() == 1 && Storage.files[ink(96, 140, true)] == before);
    assert(!Storage.exists((ink(96, 140, true) + ".davtmp").c_str()) && !coverCopy());
    assert(step(b) == O::Failed && step(b) == O::Failed && status() == S::GaveUp && step(b) == O::Nothing);
  }
  std::puts("GREEN card full while writing: old cover kept, counted to 3, then given up");

  // What may pass is not counted: the heap floor at either big block, a failed copy, a failed read,
  // a read that fails in the middle of the decode.
  for (const char* who : {"ink-bounds", "pjx-band", "pjx-readers"}) {
    fresh();
    Book b{jpeg};
    refuseWho = who;
    assert(step(b) == O::NoHeap && failures() == 0 && status() == S::Pending && !coverCopy());
    refuseWho = nullptr;
    assert(step(b) == O::Sharp);
  }
  {
    fresh();
    Book b{jpeg};
    b.copyFails = progluma::Result::Io;
    assert(step(b) == O::Later && failures() == 0 && !coverCopy());
    b.copyFails = progluma::Result::Done;
    Storage.readFails = 3;
    assert(step(b) == O::Later && failures() == 0);
    Storage.watchReads = 0;
    Storage.watchFailAt = readsWhole - 2;  // late in the decode, past the marker pass
    assert(step(b) == O::Later && failures() == 0 && status() == S::Pending && !coverCopy());
    Storage.watchFailAt = 0;
    assert(step(b) == O::Sharp);
  }
  std::puts("GREEN heap floor refusal, copy and read failures, a read failing mid decode: not counted, then sharp");

  // A heap that cannot hold the decode is seen before the cover is copied out of the book.
  {
    fresh();
    Book b{jpeg};
    ESP.freeHeap = 20000;  // the decode would leave the floor short
    assert(step(b) == O::NoHeap && b.copies == 0 && failures() == 0 && b.sniffs == 1);
    ESP.freeHeap = 200000;
    assert(step(b) == O::Sharp && b.copies == 1);
  }
  std::puts("GREEN a heap under the forecast: no copy out of the book, not counted");

  // A power cut at every change of a whole step: Home's and the writer's read (coverink::restore)
  // always finds a whole cover, never marked S before it is the sharp one; a later step finishes.
  fresh();
  Book probe{jpeg};
  assert(step(probe) == O::Sharp);
  const int changes = Storage.changes;
  for (int cut = 1; cut <= changes; ++cut) {
    fresh();
    Book b{jpeg};
    Storage.cutAt = cut;
    bool threw = false;
    try {
      step(b);
    } catch (const PowerCut&) {
      threw = true;
    }
    Storage.cutAt = 0;
    assert(threw);
    const std::string path = ink(96, 140, true);
    assert(coverink::restore(path) && Storage.exists(path.c_str()));
    const auto& now = Storage.files[path];
    assert(now == sharp || now == blurred(96, 140, true));
    if (status() == S::Sharp) assert(now == sharp && cardsGone());
    for (int i = 0; i < 4 && status() != S::Sharp; ++i) step(b);
    assert(status() == S::Sharp && Storage.files[path] == sharp && cardsGone());
  }
  std::printf("GREEN power cut at each of %d changes: a whole cover always there, S only on the sharp one\n", changes);
  return 0;
}
