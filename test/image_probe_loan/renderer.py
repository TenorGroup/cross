#!/usr/bin/env python3
"""Exercise the production framebuffer-loan lifecycle with an instrumented display."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('source', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
cpp = (args.source / 'lib/GfxRenderer/GfxRenderer.cpp').read_text()

def function(signature):
    start = cpp.index(signature)
    brace = cpp.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (cpp[end] == '{') - (cpp[end] == '}')
        end += 1
    return cpp[start:end]

functions = [function(name) for name in (
    'void GfxRenderer::releaseFrameBufferForBuild()',
    'bool GfxRenderer::restoreFrameBufferAfterBuild()',
    'GfxRenderer::FrameBufferLoan::FrameBufferLoan(',
    'void GfxRenderer::FrameBufferLoan::end()')]
fixture = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#define LOG_ERR(...) ((void)0)
struct { void restart() { assert(false); } } ESP;
namespace buildscratch {
uint8_t* scratch = nullptr;
void lend(uint8_t* p, uint32_t size) { assert(size == 64); scratch = p; }
void reclaim() { scratch = nullptr; }
}
struct Display {
  uint8_t bytes[64]{};
  bool lent = false;
  unsigned returned = 0;
  uint8_t* lendFrameBufferStorage(uint32_t* size) {
    assert(!lent); lent = true; *size = 64; return bytes;
  }
  void returnFrameBufferStorage() {
    assert(lent); lent = false; ++returned; std::fill_n(bytes, 64, 255);
  }
  uint8_t* getFrameBuffer() { return lent ? nullptr : bytes; }
};
class GfxRenderer {
 public:
  Display display;
  uint8_t* frameBuffer = display.bytes;
  // This observation field also makes the old release path's missing increment measurable.
  uint32_t frameBufferLoans = 0;
  void releaseFrameBufferForBuild();
  bool restoreFrameBufferAfterBuild();
  bool hasFrameBuffer() const { return frameBuffer != nullptr; }
  class FrameBufferLoan {
   public:
    explicit FrameBufferLoan(GfxRenderer& renderer);
    ~FrameBufferLoan() { end(); }
    void end();
   private:
    GfxRenderer& renderer_;
    bool active_ = false;
  };
};
@@FUNCTIONS@@
int main() {
  unsigned passed = 0;
  auto check = [&](bool condition, const char* name) {
    std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
    passed += condition;
  };
  GfxRenderer renderer;
  {
    GfxRenderer::FrameBufferLoan outer(renderer);
    { GfxRenderer::FrameBufferLoan inner(renderer); }
    check(!renderer.hasFrameBuffer() && buildscratch::scratch == renderer.display.bytes &&
          renderer.frameBufferLoans == 1 && renderer.display.returned == 0,
          "nested loan stays with outer owner and increments once");
  }
  check(renderer.hasFrameBuffer() && !buildscratch::scratch && renderer.display.returned == 1 &&
        renderer.display.bytes[0] == 255, "outer release returns a white framebuffer");
  {
    GfxRenderer::FrameBufferLoan second(renderer);
    second.end(); second.end();
    check(renderer.frameBufferLoans == 2 && renderer.display.returned == 2 && renderer.hasFrameBuffer(),
          "explicit end and destructor restore once");
  }
  std::cout << passed << "/3 framebuffer-loan paths pass\n";
  return passed == 3 ? 0 : 1;
}
'''
args.output.mkdir(parents=True, exist_ok=True)
projected = args.output / 'renderer.cpp'
projected.write_text(fixture.replace('@@FUNCTIONS@@', '\n'.join(functions)))
binary = args.output / 'renderer'
subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                '-g', str(projected), '-o', str(binary)], check=True)
raise SystemExit(subprocess.run([str(binary)]).returncode)
