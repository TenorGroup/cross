import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def extract(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


parser = argparse.ArgumentParser()
parser.add_argument("--source", type=Path)
args = parser.parse_args()
repo = Path(__file__).resolve().parents[2]
source = (args.source or repo / "src/components/TenorMenuChrome.cpp").read_text()
strip = extract(source, "void drawStripMiddle(")
icon = extract(strip, "const auto icon =") + ";"
icon = re.sub(r"\br\b", "renderer", icon)
icon = re.sub(r"\bx\b", "iconX", icon)
metric = extract(source, "tenorchrome::BatteryInkBounds tenorchrome::batteryInkBounds(")
painter = extract(source, "void tenorchrome::drawBarIcon(")
wrapper = extract(source, "void drawIcon(")
fixture = r'''
#include <algorithm>
#include <cstdio>
#include <utility>
#include <vector>
#include "components/icons/tenorStatusIcons.h"
namespace EpdFontFamily { enum Style { REGULAR }; }
struct GfxRenderer {
  mutable std::vector<std::pair<int, int>> pixels;
  int getTextInkTop(int, const char*, EpdFontFamily::Style) const { return 7; }
  int getTextInkBottom(int, const char*, EpdFontFamily::Style) const { return 21; }
  void drawPixel(int column, int row, bool) const { pixels.emplace_back(column, row); }
};
namespace tenorchrome {
struct BatteryInkBounds { int top; int height; };
BatteryInkBounds batteryInkBounds(const GfxRenderer&, int, int);
void drawBarIcon(const GfxRenderer&, const uint8_t*, int, int, int, int, bool);
}
''' + metric + painter + wrapper + r'''
int main() {
  GfxRenderer renderer;
  constexpr int STRIP_ICON = 18, STRIP_ICON_GAP = 6;
  const int iconY = 11;
  const auto batteryInk = tenorchrome::batteryInkBounds(renderer, 0, 8);
  int iconX = 401;
''' + icon + r'''
  int failures = 0;
  const freeink::Icon* icons[] = {&icon_status_wifi_18, &icon_status_bluetooth_18, &icon_status_bluetooth_lost_18};
  for (int index = 0; index < 3; ++index) {
    renderer.pixels.clear();
    iconX = 401;
    icon(*icons[index]);
    int top = 800, bottom = -1;
    for (const auto& pixel : renderer.pixels) {
      top = std::min(top, pixel.second);
      bottom = std::max(bottom, pixel.second);
    }
    const bool passed = bottom == 28 && iconX == 377;
    failures += !passed;
    std::printf("%s radio=%d ink=%d-%d next_x=%d\n", passed ? "GREEN" : "RED", index, top, bottom, iconX);
  }
  return failures ? 1 : 0;
}
'''
with tempfile.TemporaryDirectory(prefix="touch-strip-ink-") as folder:
    folder = Path(folder)
    cpp = folder / "fixture.cpp"
    program = folder / "fixture"
    cpp.write_text(fixture)
    subprocess.run(["c++", "-std=c++17", "-I", str(repo / "src"), "-I",
                    str(repo / "freeink-sdk/libs/assets/Icons/include"), str(cpp), "-o", str(program)], check=True)
    raise SystemExit(subprocess.run([str(program)]).returncode)
