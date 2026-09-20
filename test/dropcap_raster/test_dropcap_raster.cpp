#include <DropCapReconstruction.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <utility>
#include <vector>
using dropcap_reconstruction::Bitmap;
#define CHECK(condition) do { if (!(condition)) { \
  std::fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #condition); std::exit(3); \
} } while (false)

static std::vector<uint8_t> pack(const std::vector<uint8_t>& pixels, int bpp) {
  std::vector<uint8_t> bytes((pixels.size() * bpp + 7) / 8, 0);
  for (size_t i = 0; i < pixels.size(); ++i) bytes[i * bpp / 8] |=
      (bpp == 2 ? pixels[i] : pixels[i] != 0) << (8 - bpp - (i * bpp) % 8);
  return bytes;
}
static std::vector<uint8_t> baseline(const Bitmap& b, int w, int h) {
  std::vector<uint8_t> out(w * h);
  for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
    const double fx = (((2 * x + 1) * b.width * 128 / w) - 128) / 256.0;
    const double fy = (((2 * y + 1) * b.height * 128 / h) - 128) / 256.0;
    const int sx = static_cast<int>(std::floor(fx)), sy = static_cast<int>(std::floor(fy));
    const double dx = fx - sx, dy = fy - sy;
    out[y * w + x] = static_cast<uint8_t>(std::round(b.sample(sx, sy) * (1-dx)*(1-dy) +
        b.sample(sx+1, sy) * dx*(1-dy) + b.sample(sx, sy+1) * (1-dx)*dy + b.sample(sx+1, sy+1) * dx*dy));
  }
  return out;
}
static int countRegions(const std::vector<uint8_t>& pixels, int w, int h, bool foreground, bool diagonal) {
  const int pw = w + 2, ph = h + 2;
  std::vector<uint8_t> visited(pw * ph, 0);
  const auto filled = [&](int x, int y) {
    const bool ink = x > 0 && y > 0 && x <= w && y <= h && pixels[(y-1)*w+x-1] != 0;
    return foreground ? ink : !ink;
  };
  int count = 0;
  for (int y = 0; y < ph; ++y) for (int x = 0; x < pw; ++x) {
    const int index = y * pw + x;
    if (visited[index] || !filled(x, y)) continue;
    ++count;
    std::vector<std::pair<int,int>> pending{{x,y}};
    visited[index] = 1;
    while (!pending.empty()) {
      const auto p = pending.back(); pending.pop_back();
      for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
        if ((!dx && !dy) || (!diagonal && dx && dy)) continue;
        const int xx = p.first + dx, yy = p.second + dy;
        if (xx < 0 || yy < 0 || xx >= pw || yy >= ph || visited[yy*pw+xx] || !filled(xx, yy)) continue;
        visited[yy*pw+xx] = 1; pending.emplace_back(xx, yy);
      }
    }
  }
  return count;
}
static std::pair<int,int> topology(const std::vector<uint8_t>& p, int w, int h) {
  return {countRegions(p,w,h,true,true), countRegions(p,w,h,false,false)-1};
}
static int accepted = 0, rejected = 0, cases = 0;
static void check(const std::vector<uint8_t>& source, int sw, int sh, int bpp, int w, int h) {
  auto bytes = pack(source, bpp); const auto unchanged = bytes;
  Bitmap b{bytes.data(), sw, sh, bpp == 2};
  const auto expected = baseline(b,w,h);
  std::vector<uint8_t> out(w*h, 255), repeated(w*h, 255), fallback(w*h, 255);
  size_t emitted = 0;
  const auto emit = [&](int x, int y, int ink) {
    CHECK(x >= 0 && y >= 0 && x < w && y < h && ink >= 0 && ink <= 3);
    CHECK(emitted == static_cast<size_t>(y*w+x)); ++emitted; out[y*w+x] = ink;
  };
  const bool used = dropcap_reconstruction::render(b,w,h,emit);
  used ? ++accepted : ++rejected;
  CHECK(emitted == out.size());
  if (topology(out,w,h) != topology(expected,w,h)) {
    std::fprintf(stderr,"topology mismatch case=%d source=%dx%d dest=%dx%d bpp=%d\n",cases,sw,sh,w,h,bpp);
    std::exit(2);
  }
  const bool failedUsed = dropcap_reconstruction::render(b,w,h,[&](int x,int y,int ink){ fallback[y*w+x]=ink; },true);
  CHECK(!failedUsed && fallback == expected);
  const bool repeatedUsed = dropcap_reconstruction::render(b,w,h,[&](int x,int y,int ink){ repeated[y*w+x]=ink; });
  CHECK(used == repeatedUsed && out == repeated && bytes == unchanged);
  if (w <= sw && h <= sh) CHECK(!used && out == expected);
  if (w > dropcap_reconstruction::maxWidth || h > dropcap_reconstruction::maxHeight) CHECK(!used && out == expected);
  ++cases;
}
int main() {
  static_assert(dropcap_reconstruction::maxScratchBytes == 5156,"scratch bound drift");
  CHECK(dropcap_reconstruction::scratchBytes(256,256) == 5156);
  std::mt19937 rng(108);
  for (int bpp : {1,2}) {
    for (int i = 0; i < 180; ++i) {
      const int sw = 1 + rng()%22, sh = 1 + rng()%23;
      std::vector<uint8_t> p(sw*sh);
      for (auto& v : p) v = bpp == 2 ? rng()%4 : (rng()%2)*3;
      check(p,sw,sh,bpp,sw*2+1,sh*2+3);
      if (i < 12) { check(p,sw,sh,bpp,sw,sh); check(p,sw,sh,bpp,1,1); }
    }
    for (int v = 0; v < 4; ++v) {
      if (bpp == 1 && v != 0 && v != 3) continue;
      check({static_cast<uint8_t>(v)},1,1,bpp,1,1);
      check({static_cast<uint8_t>(v)},1,1,bpp,256,256);
      check({static_cast<uint8_t>(v)},1,1,bpp,257,258);
    }
    std::vector<uint8_t> wide(256*255);
    for (auto& v : wide) v = bpp == 2 ? rng()%4 : (rng()%2)*3;
    check(wide,256,255,bpp,256,256);
  }
  size_t emitted=0;
  CHECK(!dropcap_reconstruction::render({nullptr,1,1,true},1,1,[&](int,int,int){++emitted;}));
  uint8_t zero = 0;
  CHECK(!dropcap_reconstruction::render({&zero,0,1,true},1,1,[&](int,int,int){++emitted;}));
  CHECK(!dropcap_reconstruction::render({&zero,1,1,true},0,1,[&](int,int,int){++emitted;}));
  CHECK(emitted==0 && accepted>0 && rejected>0);
  std::printf("PASS cases=%d accepted=%d baseline=%d scratch_limit=%zu\n",cases,accepted,rejected,dropcap_reconstruction::maxScratchBytes);
}
