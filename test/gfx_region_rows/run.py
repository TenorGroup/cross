"""GfxRenderer's region rows: every strip in order is the buffer copyRegionToBuffer fills, in all 4 orientations, on the X3 panel
(528 x 792) and the X4 panel (480 x 800); and the strips put back reproduce the screen buffer the single copy would.

Pulls the real region functions out of GfxRenderer.cpp onto a stub panel (the way test/cover_redrive does)."""
import argparse
import pathlib
import subprocess

here = pathlib.Path(__file__).resolve().parent
repo = here.parents[1]
ap = argparse.ArgumentParser()
ap.add_argument('--output', type=pathlib.Path, required=True)
ap.add_argument('--source', type=pathlib.Path, default=repo / 'lib/GfxRenderer/GfxRenderer.cpp')
a = ap.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
t = a.source.read_text()


def block(signature):
    start = t.index(signature)
    end = t.index('{', start) + 1
    depth = 1
    while depth:
        depth += (t[end] == '{') - (t[end] == '}')
        end += 1
    return t[start:end] + '\n'


code = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
class GfxRenderer {
 public:
  enum Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };
  Orientation orientation = Portrait;
  uint16_t panelWidth, panelHeight, panelWidthBytes;
  uint8_t* frameBuffer;
  size_t getRegionByteSize(int, int, int, int) const;
  bool copyRegionToBuffer(int, int, int, int, uint8_t*, size_t) const;
  bool copyBufferToRegion(int, int, int, int, const uint8_t*, size_t) const;
  bool getRegionRows(int, int, int, int, int&, int&) const;
  bool copyRegionRowsToBuffer(int, int, int, int, int, int, uint8_t*, size_t) const;
  bool copyBufferToRegionRows(int, int, int, int, int, int, const uint8_t*, size_t) const;
};
''' + block('static inline void rotateCoordinates(') + block('static bool logicalRectToPhysicalBounds(') + ''.join(block(s) for s in (
    'size_t GfxRenderer::getRegionByteSize(', 'bool GfxRenderer::copyRegionToBuffer(', 'bool GfxRenderer::copyBufferToRegion(',
    'bool GfxRenderer::getRegionRows(', 'bool GfxRenderer::copyRegionRowsToBuffer(', 'bool GfxRenderer::copyBufferToRegionRows(')) + r'''
struct Rect { int x, y, w, h; };
int main() {
  int rects = 0;
  for (auto [pw, ph] : {std::pair<int, int>{792, 528}, {800, 480}}) {
    std::vector<uint8_t> pixels(pw / 8 * ph);
    for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = uint8_t(i * 2654435761u >> 24);
    for (int o = 0; o < 4; ++o) {
      GfxRenderer r;
      r.orientation = GfxRenderer::Orientation(o); r.panelWidth = pw; r.panelHeight = ph; r.panelWidthBytes = pw / 8;
      r.frameBuffer = pixels.data();
      const bool portrait = o == GfxRenderer::Portrait || o == GfxRenderer::PortraitInverted;
      const int lw = portrait ? ph : pw, lh = portrait ? pw : ph;
      for (Rect rect : {Rect{24, 40, lw - 48, 664}, Rect{0, 40, lw, 664}, Rect{3, 5, 21, 7}, Rect{lw - 9, lh - 11, 9, 11}, Rect{0, 0, 1, 1}}) {
        if (rect.x + rect.w > lw || rect.y + rect.h > lh) continue;
        ++rects;
        const size_t whole = r.getRegionByteSize(rect.x, rect.y, rect.w, rect.h);
        assert(whole > 0);
        std::vector<uint8_t> single(whole);
        assert(r.copyRegionToBuffer(rect.x, rect.y, rect.w, rect.h, single.data(), single.size()));
        int perRow = 0, rows = 0;
        assert(r.getRegionRows(rect.x, rect.y, rect.w, rect.h, perRow, rows));
        assert(size_t(perRow) * rows == whole);
        for (int stripRows : {1, 7, 4096 / perRow, rows}) {
          if (stripRows < 1) stripRows = 1;
          std::vector<uint8_t> joined;
          std::vector<uint8_t> strip(size_t(stripRows) * perRow);
          for (int first = 0; first < rows; first += stripRows) {
            const int n = std::min(stripRows, rows - first);
            assert(r.copyRegionRowsToBuffer(rect.x, rect.y, rect.w, rect.h, first, n, strip.data(), strip.size()));
            joined.insert(joined.end(), strip.begin(), strip.begin() + size_t(n) * perRow);
          }
          assert(joined == single);
          // The strips put back: the screen buffer ends as one copy back would leave it, nothing else touched.
          std::vector<uint8_t> saved = pixels, expected = pixels;
          std::vector<uint8_t> noise(whole, 0x3C);
          r.frameBuffer = expected.data();
          assert(r.copyBufferToRegion(rect.x, rect.y, rect.w, rect.h, noise.data(), noise.size()));
          r.frameBuffer = pixels.data();
          for (int first = 0; first < rows; first += stripRows) {
            const int n = std::min(stripRows, rows - first);
            std::fill(strip.begin(), strip.end(), 0x3C);
            assert(r.copyBufferToRegionRows(rect.x, rect.y, rect.w, rect.h, first, n, strip.data(), strip.size()));
          }
          assert(pixels == expected);
          pixels = saved;
        }
        // Out of the region, or a buffer too small: refused, nothing written.
        uint8_t one[1] = {0};
        assert(!r.copyRegionRowsToBuffer(rect.x, rect.y, rect.w, rect.h, rows, 1, one, sizeof(one)));
        assert(!r.copyRegionRowsToBuffer(rect.x, rect.y, rect.w, rect.h, 0, rows + 1, single.data(), single.size()));
        assert(!r.copyRegionRowsToBuffer(rect.x, rect.y, rect.w, rect.h, -1, 1, single.data(), single.size()));
        assert(perRow < 2 || !r.copyRegionRowsToBuffer(rect.x, rect.y, rect.w, rect.h, 0, 1, one, sizeof(one)));
        assert(!r.copyBufferToRegionRows(rect.x, rect.y, rect.w, rect.h, 0, rows + 1, single.data(), single.size()));
      }
    }
  }
  std::printf("GREEN %d rects x 4 orientations x 2 panels: strips join to the single copy and put back the same\n", rects);
}
'''
p = a.output / 'rows.cpp'
p.write_text(code)
subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-UNDEBUG', '-fsanitize=address,undefined', str(p),
                '-o', str(a.output / 'rows')], check=True)
raise SystemExit(subprocess.run([str(a.output / 'rows')]).returncode)
