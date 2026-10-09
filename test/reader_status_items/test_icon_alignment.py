"""RED/GREEN fixture for reader status icon and text vertical alignment.

The harness copies the production three-slot painter, records its observable
pixel bounds, and compares the bookmark pin, battery body, and clock glyph
centres in the same status lane. It runs for the X3, X4, and X4 Pro profile
labels because the normal status painter is shared by those targets.
"""
from pathlib import Path
import argparse
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
parser.add_argument("--green", action="store_true")
args = parser.parse_args()
repo = args.repo


def extract_function(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


body = extract_function(
    (repo / "src/components/TenorMenuChrome.cpp").read_text(),
    "void tenorchrome::drawReaderSlots(",
)
metric_body = extract_function(
    (repo / "src/components/TenorMenuChrome.cpp").read_text(),
    "tenorchrome::BatteryInkBounds tenorchrome::batteryInkBounds(",
)

cpp = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "activities/reader/ReaderStatusLayout.h"
constexpr int SMALL_FONT_ID = 0;
struct EpdFontFamily { enum Style { REGULAR }; };
struct CrossPointSettings {
  enum READER_STATUS_SLOT : unsigned char {
    STATUS_SLOT_NONE = 0, STATUS_SLOT_CLOCK, STATUS_SLOT_BATTERY,
    STATUS_SLOT_CHAPTER_PAGES, STATUS_SLOT_BOOK_PERCENT,
    STATUS_SLOT_CHAPTER_ETA, STATUS_SLOT_BOOK_ETA, STATUS_SLOT_COUNT
  };
  enum STATUS_BAR_TITLE : unsigned char { BOOK_TITLE = 0, CHAPTER_TITLE, HIDE_TITLE };
  struct StatusBarSpec {
    bool slotsEnabled = true;
    unsigned char topTitleMode = HIDE_TITLE;
    std::array<unsigned char, 3> slots{STATUS_SLOT_NONE, STATUS_SLOT_NONE, STATUS_SLOT_NONE};
    bool textLaneVisible(bool) const { return true; }
  } spec;
  int clockFormat = 0;
  bool hidden = false;
  bool percent = true;
  StatusBarSpec statusBarSpec() const { return spec; }
  bool readerStatusBarHidden() const { return hidden; }
  bool batteryPercentShown(bool) const { return percent; }
} SETTINGS;
struct Ink {
  enum Kind { Text, Battery, Fill, Star } kind;
  int x, y, w, h;
  int baseline = 0;
  std::string label;
};
std::vector<Ink> ink;
bool rough = false;
struct GfxRenderer {
  int w = 480;
  int h = 800;
  int digitTop = 5;
  int digitBottom = 17;
  int getScreenWidth() const { return w; }
  int getScreenHeight() const { return h; }
  int getTextWidth(int, const char* text) const { return static_cast<int>(std::strlen(text)) * 8; }
  int getTextInkTop(int, const char* value, int) const { assert(std::strcmp(value, "0123456789") == 0); return digitTop; }
  int getTextInkBottom(int, const char* value, int) const { assert(std::strcmp(value, "0123456789") == 0); return digitBottom; }
  std::string truncatedText(int, const char* text, int width) const {
    std::string out = text ? text : "";
    while (getTextWidth(0, out.c_str()) > width && !out.empty()) out.pop_back();
    return out;
  }
  void drawText(int, int x, int y, const char* text) const {
    if (!text || y > 100) ink.push_back({Ink::Text, x, y + 5, getTextWidth(0, text), 12, y, text});
  }
  void drawRect(int x, int y, int width, int height, int, bool) const {
    ink.push_back({Ink::Battery, x, y, width, height, 0, "battery"});
  }
  void fillRect(int x, int y, int width, int height) const {
    ink.push_back({Ink::Fill, x, y, width, height, 0, "fill"});
  }
};
namespace shell { bool uglyParts() { return rough; } }
namespace ugly {
  enum class Size { S22 };
  int width(const GfxRenderer& r, Size, const char* text) { return r.getTextWidth(0, text); }
  std::string fit(const GfxRenderer& r, Size, const char* text, int room) { return r.truncatedText(0, text, room); }
  void text(const GfxRenderer& r, Size, int x, int y, const char* value) { r.drawText(0, x, y - 18, value); }
  void battery(const GfxRenderer&, int x, int y, int) { ink.push_back({Ink::Battery, x, y - 9, 40, 19, 0, "ugly-battery"}); }
}
namespace clockstatus { bool valid = true; bool hasValidTime() { return valid; } }
struct Clock { bool formatTime(char* out, size_t size, bool) { std::snprintf(out, size, "12:34"); return true; } } halClock;
struct Power { int getDisplayedBatteryPercentage() { return 100; } } powerManager;
struct Gpio { bool charging = false; bool isUsbConnected() { return charging; } } gpio;
namespace inlineSymbols {
  enum class Shape { Star };
  void drawShape(const GfxRenderer&, Shape, int x, int y, int size, bool) {
    const int half = std::max(3, size / 2);
    ink.push_back({Ink::Star, x - half, y - half, half * 2 + 1, half * 2 + 1, 0, "star"});
  }
  int markTopOnCapitals(const GfxRenderer&, int, int y, int height) {
    return y + (height == 14 ? 4 : 6);
  }
}
void drawChargingBolt(const GfxRenderer&, int, int, int, int, bool) {}
namespace tenorchrome {
  struct BatteryInkBounds { int top; int height; };
  BatteryInkBounds batteryInkBounds(const GfxRenderer&, int, int);
  int statusTextY(int height, bool) { return height - 24; }
  void drawReaderSlots(const GfxRenderer&, const char*, int, int, float, bool, bool, int64_t, int64_t);
}
''' + metric_body + '\n' + body + r'''

static double center(const Ink& box) { return box.y + (box.h - 1) / 2.0; }

int main() {
  using S = CrossPointSettings;
  for (const auto bounds : {std::array<int, 2>{3, 15}, {8, 26}, {10, 34}}) {
    GfxRenderer renderer;
    renderer.digitTop = bounds[0];
    renderer.digitBottom = bounds[1];
    const auto box = tenorchrome::batteryInkBounds(renderer, SMALL_FONT_ID, 42);
    assert(box.top == 42 + bounds[0] && box.height == bounds[1] - bounds[0]);
  }
  int cases = 0;
  for (const char* profile : {"X3", "X4", "X4PRO"}) {
    (void)profile;
    for (const int screen : {480, 528, 800}) {
      for (const int item : {S::STATUS_SLOT_CLOCK, S::STATUS_SLOT_BATTERY}) {
        GfxRenderer renderer;
        renderer.w = screen;
        SETTINGS.spec = {};
        SETTINGS.spec.slotsEnabled = true;
        SETTINGS.spec.topTitleMode = S::HIDE_TITLE;
        SETTINGS.spec.slots[0] = static_cast<unsigned char>(item);
        ink.clear();
        tenorchrome::drawReaderSlots(renderer, "Chapter", 5, 25, 43.0f, false, true, 5400, 25200);
        const int textBaseline = renderer.h - 24;
        const double textCenter = textBaseline + 10.5;  // Geist 8 H: top +5, bottom +16.
        double minCenter = 1000, maxCenter = -1000;
        bool sawPin = false, sawBattery = false, sawClock = false;
        for (const auto& box : ink) {
          if (box.kind == Ink::Star) { sawPin = true; minCenter = std::min(minCenter, center(box)); maxCenter = std::max(maxCenter, center(box)); }
          if (box.kind == Ink::Battery) {
            sawBattery = true;
            if (box.y != textBaseline + 5 || box.y + box.h != textBaseline + 17) {
              std::fprintf(stderr, "battery edges diverge: %d..%d, digits %d..%d\n",
                           box.y, box.y + box.h - 1, textBaseline + 5, textBaseline + 16);
              return 1;
            }
            minCenter = std::min(minCenter, center(box)); maxCenter = std::max(maxCenter, center(box));
          }
          if (box.kind == Ink::Text && box.baseline == textBaseline) { sawClock = true; minCenter = std::min(minCenter, center(box)); maxCenter = std::max(maxCenter, center(box)); }
        }
        std::printf("profile=%s screen=%d item=%d baseline=%d pin=%.1f battery=%s clock=%s span=%.1f\n",
                    profile, screen, item, textBaseline, sawPin ? center(*std::find_if(ink.begin(), ink.end(), [](const Ink& b) { return b.kind == Ink::Star; })) : -1.0,
                    sawBattery ? "seen" : "none", sawClock ? "seen" : "none", maxCenter - minCenter);
        if (maxCenter - minCenter > 1.0) {
          std::fprintf(stderr, "status icon vertical bounds diverge: span %.1f px\n", maxCenter - minCenter);
          return 1;
        }
        ++cases;
      }
    }
  }
  std::printf("status_icon_alignment:GREEN (%d profile/width/item cases)\n", cases);
}
'''

with tempfile.TemporaryDirectory(prefix="status-icon-alignment-") as folder:
    source = Path(folder) / "fixture.cpp"
    binary = Path(folder) / "fixture"
    source.write_text(cpp)
    subprocess.run(["c++", "-std=c++20", "-I", str(repo / "src"), str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
