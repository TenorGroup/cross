#!/usr/bin/env python3
"""Exercise the status-bar path while a framebuffer loan is active."""
import argparse
from pathlib import Path
import subprocess


parser = argparse.ArgumentParser()
parser.add_argument("source", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()

reader = (args.source / "src/activities/reader/EpubReaderActivity.cpp").read_text()
start = reader.index("void EpubReaderActivity::repaintStatusBarAlone()")
brace = reader.index("{", start)
depth = 1
end = brace + 1
while depth:
    depth += (reader[end] == "{") - (reader[end] == "}")
    end += 1
body = reader[brace + 1:end - 1]

fixture = r'''
#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace HalDisplay { enum RefreshMode { FAST_REFRESH }; }
namespace CrossPointSettings { constexpr int HIDE_TITLE = 1; }
struct StatusBarSpec {
  bool showBattery = true;
  bool slotsEnabled = false;
  int topTitleMode = 0;
  bool showsTitle() const { return false; }
};
struct Settings {
  StatusBarSpec statusBarSpec() const { return {}; }
  bool readerStatusBarHidden() const { return false; }
} SETTINGS;
struct Scope { void endScanAndPrewarm() {} };
struct FontCacheManager { Scope createPrewarmScope() { return {}; } };
struct Renderer {
  bool loanActive = false;
  uint8_t pixels[64];
  int displays = 0;
  Renderer() { std::fill(std::begin(pixels), std::end(pixels), 255); }
  bool hasFrameBuffer() const { return !loanActive; }
  int getScreenWidth() const { return 8; }
  int getScreenHeight() const { return 8; }
  void fillRect(int, int, int, int, bool) {}
  FontCacheManager* getFontCacheManager() { static FontCacheManager manager; return &manager; }
  void displayBuffer(HalDisplay::RefreshMode) { ++displays; }
};
struct Gpio {
  bool isUsbConnected() const { return true; }
} gpio;
namespace bleturner {
enum class LinkNote { None };
inline LinkNote linkNote() { return LinkNote::None; }
}
namespace tenorchrome {
inline bool enabled() { return true; }
inline int readerStatusTop(int) { return 0; }
}
struct EpubReaderActivity {
  Renderer renderer;
  bool pageFrameShown = true;
  bool preview = false;
  bool pageFrameUsb = false;
  bool pageFrameKeepsUnderFast = true;
  bleturner::LinkNote linkNoteDrawn = bleturner::LinkNote::None;
  int requests = 0;
  int readerStatusTopReserve() const { return 0; }
  void requestUpdate() { ++requests; }
  void renderStatusBar() {}
  void repaintStatusBarAlone();
};
#define LOG_DBG(...) ((void)0)
void EpubReaderActivity::repaintStatusBarAlone() {
''' + body + r'''
}

int main() {
  EpubReaderActivity reader;
  reader.renderer.loanActive = true;
  reader.repaintStatusBarAlone();
  if (reader.renderer.displays != 0) {
    std::fprintf(stderr, "FAIL: status repaint used a lent framebuffer\n");
    return 1;
  }
  reader.renderer.loanActive = false;
  reader.repaintStatusBarAlone();
  if (reader.renderer.displays != 1) {
    std::fprintf(stderr, "FAIL: status repaint did not resume with a framebuffer\n");
    return 1;
  }
  std::puts("GREEN: USB status repaint waits for a framebuffer loan to end");
  return 0;
}
'''

args.output.mkdir(parents=True, exist_ok=True)
projected = args.output / "repaint.cpp"
projected.write_text(fixture)
binary = args.output / "repaint"
subprocess.run([
    "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
    "-g", str(projected), "-o", str(binary)
], check=True)
raise SystemExit(subprocess.run([str(binary)]).returncode)
