
// ---- cases: the X3 after a Bluetooth session has 93 KB free heap and a largest block of 28.660 B. ----
int failures = 0, checks = 0;
void check(bool ok, const std::string& name) { ++checks; if (!ok) { ++failures; std::cout << "FAIL " << name << '\n'; } }
struct Box { int x, y, w, h; };
constexpr Box SHELF{24, 40, 480, 664};   // 39.840 B
constexpr Box LARGE{0, 40, 528, 664};    // the neighbours' covers reach the screen edges
constexpr uint8_t SENTINEL = 0xA5;
const std::string PATH = "/.crosspoint/home-frame-2-0.card";
constexpr uint32_t KEY = 0xC0FFEE;
#ifndef LARGEST
#define LARGEST 28660
#endif

std::vector<uint8_t> region(const Box& b) {
  std::vector<uint8_t> out(gRenderer.getRegionByteSize(b.x, b.y, b.w, b.h));
  gRenderer.copyRegionToBuffer(b.x, b.y, b.w, b.h, out.data(), out.size());
  return out;
}
void paint(const Box& b, uint32_t seed) {  // a frame: every byte of the box differs from its neighbours
  gRenderer.fb.fill(SENTINEL);
  uint32_t v = seed;
  for (int y = b.y; y < b.y + b.h; ++y)
    for (int x = b.x / 8; x <= (b.x + b.w - 1) / 8; ++x) { v = v * 1664525u + 1013904223u; gRenderer.fb[y * Renderer::ROW + x] = uint8_t(v >> 24); }
}
void rectOf(HomeActivity& h, const Box& b) {
  h.coverRectX = b.x; h.coverRectY = b.y; h.coverRectW = b.w; h.coverRectH = b.h; h.textRectW = h.textRectH = 0;
}
void reset() {
  Storage = {};
  heapfloor::largest = LARGEST; heapfloor::biggestRequest = 0; heapfloor::refused = 0;
  heapfloor::peakBytes = 0; heapfloor::takesLeft = -1;
}
// The build of a frame: drawn on the screen buffer, held, then written after the panel shows it.
bool buildAndSave(const Box& b, uint32_t seed, bool settled = true) {
  paint(b, seed);
  HomeActivity h; rectOf(h, b);
  h.holdFrame(PATH, KEY, settled);
  h.saveCardFile();
  const bool onCard = h.cardOnCard;
  h.freeCoverBuffer();
  return onCard && Storage.exists(PATH.c_str());
}
size_t payloadOf(std::string& file, std::vector<uint8_t>& out) {
  if (file.size() < sizeof(CardFileHead)) { out.clear(); return 0; }
  out.assign(file.begin() + sizeof(CardFileHead), file.end());
  return out.size();
}

int main() {
  for (const Box& b : {SHELF, LARGE}) {
    const std::string name = b.w == SHELF.w ? "shelf: " : "large cover: ";
    reset();
    check(buildAndSave(b, 1), name + "the frame reaches the card with no block over 28.660 B");
    check(heapfloor::biggestRequest <= 4096, name + "the largest request is one strip, " + std::to_string(heapfloor::biggestRequest) + " B");
    check(heapfloor::liveBytes == 0, name + "no RAM held after the write");
    std::vector<uint8_t> payload;
    const auto want = region(b);
    check(Storage.exists(PATH.c_str()) && payloadOf(Storage.files[PATH], payload) == want.size() &&
              std::equal(payload.begin(), payload.end(), want.begin()),
          name + "the file holds the region byte for byte");
    // Back in: another run of the firmware, the screen buffer blank (all sentinel).
    gRenderer.fb.fill(SENTINEL);
    HomeActivity reader; rectOf(reader, b);
    heapfloor::biggestRequest = 0;
    check(reader.restoreFrame(PATH, KEY), name + "the frame is read back");
    check(heapfloor::biggestRequest <= 4096 && reader.coverBuffer == nullptr && heapfloor::liveBytes == 0,
          name + "the read takes one strip and holds no block, " + std::to_string(heapfloor::biggestRequest) + " B");
    paint(b, 1);
    const auto expected = gRenderer.fb;
    gRenderer.fb.fill(SENTINEL);
    HomeActivity again; again.restoreFrame(PATH, KEY);
    check(gRenderer.fb == expected, name + "the screen buffer is as it was, outside the region too");
  }
  {  // another frame key, a short file: nothing is written to the screen buffer
    reset(); check(buildAndSave(SHELF, 2), "setup");
    gRenderer.fb.fill(SENTINEL);
    HomeActivity h;
    check(!h.restoreFrame(PATH, KEY + 1), "another key reads nothing");
    check(std::all_of(gRenderer.fb.begin(), gRenderer.fb.end(), [](uint8_t v) { return v == SENTINEL; }), "another key leaves the screen buffer alone");
    if (!Storage.files[PATH].empty()) Storage.files[PATH].pop_back();
    check(!h.restoreFrame(PATH, KEY), "a short file reads nothing");
    check(std::all_of(gRenderer.fb.begin(), gRenderer.fb.end(), [](uint8_t v) { return v == SENTINEL; }), "a short file leaves the screen buffer alone");
  }
  {  // the card fails halfway: the region is cleared, the build starts from white
    reset(); check(buildAndSave(SHELF, 3), "setup");
    gRenderer.fb.fill(SENTINEL);
    Storage.failReadAt = 7;
    HomeActivity h;
    check(!h.restoreFrame(PATH, KEY), "a read error halfway fails the read");
    check(gRenderer.cleared >= 1, "and clears what was put back");
    Storage.failReadAt = 0;
  }
  {  // the footer draws chevrons over the frame after it is held (the Large cover reaches the screen edges): the file keeps the frame
    reset();
    paint(LARGE, 4);
    const auto drawn = region(LARGE);
    HomeActivity h; rectOf(h, LARGE);
    h.holdFrame(PATH, KEY, true);
    check(h.coverBuffer == nullptr && heapfloor::liveBytes >= drawn.size() && heapfloor::liveBytes <= drawn.size() + 4096,
          "while held the frame is blocks of 4 KB, " + std::to_string(heapfloor::liveBytes) + " B for " + std::to_string(drawn.size()));
    for (int y = 190; y < 205; ++y) gRenderer.fb[(LARGE.y + y) * Renderer::ROW + 64] ^= 0xFF;
    h.saveCardFile();
    std::vector<uint8_t> payload;
    check(Storage.exists(PATH.c_str()) && payloadOf(Storage.files[PATH], payload) == drawn.size() &&
              std::equal(payload.begin(), payload.end(), drawn.begin()),
          "the file holds the frame as it was drawn, not as the footer left it");
    check(heapfloor::liveBytes == 0, "the blocks are given back when the file is written");
  }
  {  // the heap runs out after a few strips: nothing kept, nothing leaked
    reset(); heapfloor::takesLeft = 5;
    check(!buildAndSave(SHELF, 11), "a heap that gives only 5 strips keeps no frame");
    check(heapfloor::liveBytes == 0, "and holds nothing");
    heapfloor::takesLeft = -1;
  }
  {  // a cover still to be made: the frame is drawn again, no file
    reset();
    check(!buildAndSave(SHELF, 5, false), "an unsettled frame is not written");
    reset();
    paint(SHELF, 5);
    HomeActivity h; rectOf(h, SHELF); h.touchThumbCount = 1;
    h.holdFrame(PATH, KEY, true); h.saveCardFile();
    check(!Storage.exists(PATH.c_str()), "a frame with a thumbnail queued is not written");
  }
  {  // the strip itself is refused (a heap under the door): nothing kept, nothing broken
    reset(); heapfloor::largest = 2048;
    check(!buildAndSave(SHELF, 6), "no strip, no file");
    check(heapfloor::liveBytes == 0, "and nothing held");
    heapfloor::largest = LARGEST;
  }
  {  // a write that fails midway leaves the file already there
    reset(); check(buildAndSave(SHELF, 7), "setup");
    const std::string old = Storage.files[PATH];
    for (const char* fault : {"payload", "sync"}) {
      Storage.fault = fault; Storage.writes = 0;
      check(!buildAndSave(SHELF, 8), std::string("a failed write is not a save: ") + fault);
      Storage.fault.clear();
      check(Storage.files.count(PATH) && Storage.files[PATH] == old && !Storage.exists(PATH + ".davtmp"), std::string("the old frame stays, no staging left: ") + fault);
    }
  }
  {  // a newer frame replaces the old one
    reset(); check(buildAndSave(SHELF, 9), "setup");
    check(buildAndSave(SHELF, 10), "second save");
    std::vector<uint8_t> payload;
    paint(SHELF, 10);
    const auto want = region(SHELF);
    check(payloadOf(Storage.files[PATH], payload) == want.size() && std::equal(payload.begin(), payload.end(), want.begin()), "the file holds the newer frame");
    check(!Storage.exists(PATH + ".davbak"), "no backup left");
  }
  std::cout << checks << " checks, " << failures << " failures, largest request " << heapfloor::biggestRequest << " B, peak " << heapfloor::peakBytes << " B\n";
  return failures ? 1 : 0;
}
