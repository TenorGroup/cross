#include <Arduino.h>
#include <HalStorage.h>
#include <HalMemory.h>
#include <JPEGDEC.h>
#include <JpegToBmpConverter.h>
#include <ProgressiveJpeg.h>
#include <TenorInk.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <vector>

EspClass ESP;
namespace {
constexpr size_t capacity = 8 * 1024 * 1024;
size_t live = 0, peak = 0, largest = capacity;
bool hasPsram = true;
int calls = 0, failAt = -1;
std::map<uint8_t*, size_t> allocations;
struct Output : Print {
  std::vector<uint8_t> bytes;
  size_t write(uint8_t b) override { bytes.push_back(b); return 1; }
  size_t write(const uint8_t* b, size_t n) override { bytes.insert(bytes.end(), b, b + n); return n; }
};
std::vector<uint8_t> load(const char* path) {
  std::ifstream f(path, std::ios::binary);
  return {(std::istreambuf_iterator<char>(f)), {}};
}
int le32(const std::vector<uint8_t>& b, int p) {
  return b[p] | b[p+1]<<8 | b[p+2]<<16 | b[p+3]<<24;
}
}
HalMemory::HeapStats HalMemory::getPsramHeap() { return {capacity-live, hasPsram ? capacity : 0, capacity-peak, largest}; }
HalMemory::PsramBuffer HalMemory::allocatePsram(size_t n) {
  if (calls++ == failAt || n > capacity-live || n > largest) return {};
  auto* p = static_cast<uint8_t*>(std::malloc(n));
  if (p) { allocations[p] = n; live += n; peak = std::max(peak, live); }
  return PsramBuffer(p);
}
void HalMemory::PsramDeleter::operator()(uint8_t* p) const {
  if (!p) return;
  assert(allocations.count(p)); live -= allocations.at(p); allocations.erase(p); std::free(p);
}

int main(int argc, char** argv) {
  const char* path = argc > 1 ? argv[1] : FIXTURES "/generated.jpg";
  const auto bytes = load(path);
  const std::string ref = std::string(path).substr(0, std::string(path).find_last_of('.')) + ".pgm";
  std::ifstream f(ref, std::ios::binary);
  std::string magic; int w, h, max;
  f >> magic >> w >> h >> max; f.get();
  assert(f && magic == "P5" && max == 255);
  std::vector<uint8_t> reference(w*h); f.read(reinterpret_cast<char*>(reference.data()), reference.size());
  assert(f);
  HalFile file(bytes);
  calls = 0;
  auto gray = progressivejpeg::decode(file, w, h);
  assert(gray);
  const int count = calls;
  double mse = 0;
  for (size_t i = 0; i < reference.size(); ++i) { double d = gray[i]-reference[i]; mse += d*d; }
  mse /= reference.size();
  double psnr = mse == 0 ? INFINITY : 10*std::log10(255.0*255/mse);
  std::printf("full %dx%d PSNR=%.3f dB PSRAM_peak=%zu allocations=%d\n", w, h, psnr, peak, count);
  assert(psnr >= 30);
  if (argc > 2) {
    std::ofstream o(argv[2], std::ios::binary); o << "P5\n" << w << " " << h << "\n255\n";
    o.write(reinterpret_cast<char*>(gray.get()), w*h);
  }
  for (bool four : {false, true}) {
    for (auto size : {std::pair{232,350}, std::pair{254,383}, std::pair{281,187}}) {
      Output expected, actual;
      assert(tenorink::writeCover(gray.get(), w, h, expected, size.first, size.second, four));
      assert(JpegToBmpConverter::jpegFileToInkBmpStream(file, actual, size.first, size.second, four));
      assert(actual.bytes == expected.bytes);
    }
  }
  // The screen cover uses the same full source and emits its native dimensions.
  Output screen;
  assert(JpegToBmpConverter::jpegFileToBmpStreamWithSize(file, screen, 0, 0));
  assert(le32(screen.bytes, 18) == w && le32(screen.bytes, 22) == -h);
  Output mono;
  assert(JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(file, mono, 298, 450));
  uint64_t hash = 14695981039346656037ULL;
  for (uint8_t b : mono.bytes) hash = (hash ^ b) * 1099511628211ULL;
  const std::string name(path);
  const uint64_t expected = name.find("thinking") != std::string::npos ? 0xc6f08681b08da06aULL
    : name.find("deep-work") != std::string::npos ? 0x8acc0f77c9e25269ULL : 0xabacd5cb260c44bbULL;
  assert(hash == expected);  // Ugly Home and the legacy thumbnail path retain their pixels.
  // A progressive cover the full decoder refuses (here its first PSRAM block fails, as when the image is too
  // large for PSRAM) keeps the 1/8 cover of v1.0.56 on both PSRAM paths: the tenor/ink Home cache and the screen
  // cover. Without PSRAM the same files take that 1/8 path, so its output is the expected one.
  const size_t held = live;
  for (bool four : {false, true}) {
    Output reduced, kept;
    hasPsram = false;
    assert(JpegToBmpConverter::jpegFileToInkBmpStream(file, reduced, 232, 350, four));
    hasPsram = true; calls = 0; failAt = 0;
    assert(JpegToBmpConverter::jpegFileToInkBmpStream(file, kept, 232, 350, four));
    failAt = -1;
    assert(!kept.bytes.empty() && kept.bytes == reduced.bytes && le32(kept.bytes, 18) == 232);
    assert(std::abs(le32(kept.bytes, 22)) == 350 && live == held);
  }
  {
    Output reduced, kept;
    hasPsram = false;
    assert(JpegToBmpConverter::jpegFileToBmpStreamWithSize(file, reduced, 0, 0));
    hasPsram = true; calls = 0; failAt = 0;
    assert(JpegToBmpConverter::jpegFileToBmpStreamWithSize(file, kept, 0, 0));
    failAt = -1;
    assert(kept.bytes == reduced.bytes && le32(kept.bytes, 18) == (w + 7) / 8 && le32(kept.bytes, 22) == -((h + 7) / 8));
    assert(live == held);
  }
  std::puts("GREEN: a refused full decode keeps the 1/8 cover, Home tenor/ink 2 depths and the screen cover");
  // A file built from many scans costs the decoder one walk over the image per scan. The scan cap is 64 (the
  // literal, so that the case stands without the production constant): the 1st scan repeated up to 64 scans
  // in all decodes as before; one more and the decoder reads the file as ended, the buffer is refused, and
  // both cover paths take the 1/8 cover as for any file the full decoder cannot take.
  {
    const uint8_t sos[2] = {0xff, 0xda};
    const size_t first = std::search(bytes.begin(), bytes.end(), sos, sos + 2) - bytes.begin();
    assert(first + 4 < bytes.size());
    size_t end = first + 2 + (bytes[first + 2] << 8 | bytes[first + 3]);
    while (!(bytes[end] == 0xff && bytes[end + 1] != 0 && !(bytes[end + 1] >= 0xd0 && bytes[end + 1] <= 0xd7))) ++end;
    size_t scans = 0;
    for (size_t i = 0; i + 1 < bytes.size(); ++i) scans += bytes[i] == 0xff && bytes[i + 1] == 0xda;
    const auto repeated = [&](size_t total) {
      std::vector<uint8_t> out(bytes.begin(), bytes.begin() + end);
      for (size_t n = scans; n < total; ++n) out.insert(out.end(), bytes.begin() + first, bytes.begin() + end);
      out.insert(out.end(), bytes.begin() + end, bytes.end());
      return out;
    };
    const auto atCap = repeated(64), overCap = repeated(65), flood = repeated(4000);
    HalFile capFile(atCap), overFile(overCap), floodFile(flood);
    auto atCapGray = progressivejpeg::decode(capFile, w, h);
    assert(atCapGray && std::equal(atCapGray.get(), atCapGray.get() + w * h, gray.get()));
    atCapGray.reset();
    assert(!progressivejpeg::decode(overFile, w, h) && live == held);
    assert(!progressivejpeg::decode(floodFile, w, h) && live == held);
    assert(floodFile.bytesRead < flood.size() / 4);  // the decoder stopped reading, it did not walk 4000 scans
    for (bool four : {false, true}) {
      Output reduced, kept;
      hasPsram = false;
      assert(JpegToBmpConverter::jpegFileToInkBmpStream(overFile, reduced, 232, 350, four));
      hasPsram = true;
      assert(JpegToBmpConverter::jpegFileToInkBmpStream(overFile, kept, 232, 350, four));
      assert(!kept.bytes.empty() && kept.bytes == reduced.bytes && live == held);
    }
    std::puts("GREEN: a progressive file of more than 64 scans is refused and keeps the 1/8 cover; 64 scans decode as before");
  }
  gray.reset(); assert(live == 0);
  for (int fail = 0; fail < count; ++fail) {
    calls = 0; failAt = fail;
    assert(!progressivejpeg::decode(file, w, h)); assert(live == 0);
  }
  failAt = -1;
  largest = 128;
  assert(!progressivejpeg::decode(file, w, h)); assert(live == 0);
  largest = capacity;
  assert(!progressivejpeg::decode(file, w+1, h)); assert(live == 0);
  const std::vector<uint8_t> corrupt{0xff,0xd8,0,0}; HalFile bad(corrupt);
  assert(!progressivejpeg::decode(bad, w, h)); assert(live == 0);
  hasPsram = false;
  assert(!progressivejpeg::supported() && !progressivejpeg::decode(file, w, h));
  assert(live == 0);
  std::puts("GREEN: Home parity, full screen source, legacy thumbnail parity, allocation failures, fragmentation, invalid input, absent PSRAM, no PSRAM leaks");
}
