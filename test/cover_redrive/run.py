"""Compile the renderer's actual refresh decisions with a recording panel boundary."""
import argparse
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
parser.add_argument('--out', type=Path, required=True)
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=True)
source = (args.repo / 'lib/GfxRenderer/GfxRenderer.cpp').read_text()
header = (args.repo / 'lib/GfxRenderer/GfxRenderer.h').read_text()


def block(text, signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


methods = '\n'.join(block(source, signature) for signature in [
    'HalDisplay::RefreshMode GfxRenderer::applyPromotedRefresh(',
    'HalDisplay::RefreshMode GfxRenderer::applyRedrive(',
    'void GfxRenderer::displayBuffer(',
    'void GfxRenderer::displayBufferAsync('])
members = '\n'.join(re.findall(r'  (?:mutable )?(?:bool|HalDisplay::RefreshMode) (?:redrive\w*|diffOnlyPanel_|promoted\w*)[^;]*;', header))
inline = block(header, 'void redriveNextRefresh(') + '\n' + block(header, 'void promoteNextRefresh(')
harness = r'''
#include <cassert>
#include <cstdint>
#include <vector>
template<class... T> void logStub(T...) {}
#define LOG_INF(...) logStub(__VA_ARGS__)
#define LOG_DBG(...) logStub(__VA_ARGS__)
static unsigned long start_ms = 0;
unsigned long millis() { return 0; }
struct HalDisplay {
  enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH };
  bool inverted = false;
  std::vector<uint8_t> cleanup;
  std::vector<uint8_t> displayed;
  RefreshMode mode = FAST_REFRESH;
  int refreshes = 0;
  bool isInverted() const { return inverted; }
  void cleanupGrayscaleBuffers(const uint8_t* frame) {
    if (!inverted) cleanup.assign(frame, frame + 4);
  }
  void displayBuffer(RefreshMode m, bool) { mode = m; ++refreshes; }
  void displayBufferAsync(RefreshMode m) { displayBuffer(m, false); }
};
class GfxRenderer {
 public:
  HalDisplay& display;
  uint8_t pixels[4] = {0xAA, 0x55, 0xFF, 0x00};
  uint8_t* frameBuffer = pixels;
  bool fadingFix = false;
  static inline void (*preDisplayHook)(const GfxRenderer&) = nullptr;
  GfxRenderer(HalDisplay& d) : display(d) {}
  void invertScreen() const { for (int i = 0; i < 4; ++i) frameBuffer[i] ^= 255; }
  HalDisplay::RefreshMode applyPromotedRefresh(HalDisplay::RefreshMode) const;
  HalDisplay::RefreshMode applyRedrive(HalDisplay::RefreshMode) const;
  void displayBuffer(HalDisplay::RefreshMode) const;
  void displayBufferAsync(HalDisplay::RefreshMode) const;
'''+members+'\n'+inline+r'''
};
'''+methods+r'''
void hook(const GfxRenderer& r) { r.frameBuffer[0] = 0xE7; }
int main() {
  using M = HalDisplay;
  for (bool async : {false, true}) for (bool fade : {false, true}) {
    HalDisplay panel; GfxRenderer r(panel); r.diffOnlyPanel_ = true; r.fadingFix = fade;
    auto paint = [&](M::RefreshMode mode) {
      if (async) r.displayBufferAsync(mode); else r.displayBuffer(mode);
    };
    r.redriveNextRefresh(); paint(M::FAST_REFRESH);
    assert(panel.mode == M::FULL_REFRESH && panel.refreshes == 1);
    panel.cleanup.clear(); paint(M::FAST_REFRESH);
    assert(panel.mode == M::FAST_REFRESH && panel.cleanup.empty());
    r.redriveNextRefresh(M::FAST_REFRESH); paint(M::FAST_REFRESH);
    assert(panel.mode == M::FAST_REFRESH && panel.refreshes == 3);
    for (int i = 0; i < 4; ++i) assert(panel.cleanup[i] == (r.pixels[i] ^ 255));
    r.redriveNextRefresh(M::FAST_REFRESH); r.promoteNextRefresh(M::FULL_REFRESH); paint(M::FAST_REFRESH);
    assert(panel.mode == M::FULL_REFRESH);
    r.redriveNextRefresh(M::FAST_REFRESH); r.promoteNextRefresh(M::HALF_REFRESH); paint(M::FAST_REFRESH);
    assert(panel.mode == M::HALF_REFRESH);
    r.redriveNextRefresh(M::FAST_REFRESH); paint(M::FULL_REFRESH);
    assert(panel.mode == M::FULL_REFRESH);
    GfxRenderer::preDisplayHook = hook;
    r.redriveNextRefresh(M::FAST_REFRESH); paint(M::FAST_REFRESH);
    assert(panel.cleanup[0] == (0xE7 ^ 255) && r.pixels[0] == 0xE7);
    GfxRenderer::preDisplayHook = nullptr;
    panel.inverted = true; panel.cleanup.clear();
    r.redriveNextRefresh(M::FAST_REFRESH); paint(M::FAST_REFRESH);
    assert(panel.mode == M::FAST_REFRESH && panel.cleanup.empty() && r.pixels[0] == 0xE7);
    r.redriveNextRefresh(); paint(M::FAST_REFRESH);
    assert(panel.mode == M::FULL_REFRESH && panel.cleanup.empty());
    panel.inverted = false; panel.cleanup.clear(); r.diffOnlyPanel_ = false;
    r.redriveNextRefresh(M::FAST_REFRESH); paint(M::FAST_REFRESH);
    assert(panel.mode == M::FAST_REFRESH && panel.cleanup.empty());
    r.diffOnlyPanel_ = true; r.frameBuffer = nullptr;
    r.redriveNextRefresh(M::FAST_REFRESH); paint(M::FAST_REFRESH);
    assert(panel.mode == M::FAST_REFRESH && panel.cleanup.empty());
  }
}
'''
path = args.out / 'refresh.cpp'
path.write_text(harness)
exe = args.out / 'refresh'
subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(path), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
print('PASS: 4 sync/async and fading variants, mode priority, one-shot, hook, inverted, other-panel, missing-frame')
