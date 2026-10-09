from pathlib import Path
import argparse
import subprocess


ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("--work", type=Path, required=True)
parser.add_argument("--source-ref")
args = parser.parse_args()


def read_source(path):
    if args.source_ref:
        return subprocess.check_output(
            ["git", "show", f"{args.source_ref}:{path}"], cwd=ROOT, text=True
        )
    return (ROOT / path).read_text()


def function(source, name):
    start = source.rfind("\n", 0, source.index(name)) + 1
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


header = read_source("src/components/TenorMenuChrome.h")
chrome = read_source("src/components/TenorMenuChrome.cpp")
x3_layout = read_source("src/activities/settings/X3SettingsLayout.h")
browser = read_source("src/activities/home/FileBrowserActivity.cpp")
required = (
    ("struct PathPillLayout", header),
    ("layoutPathPill(", header + chrome),
    ("drawPathPill(", header + chrome),
)
missing = [name for name, source in required if name not in source]
if missing:
    raise SystemExit("RED: path pill contract missing: " + ", ".join(missing))
callers = (
    ("X4 Pro foot bar", function(chrome, "tenorchrome::drawFootBar("), "drawPathPill("),
    ("X3 screen path", function(x3_layout, "drawPath("), "drawX3ActivityPath("),
    ("X3 file path", function(chrome, "tenorchrome::drawX3NavigationPath("), "layoutPathPill("),
)
unwired = [name for name, body, call in callers if call not in body]
if unwired:
    raise SystemExit("RED: shared path pill is not wired into: " + ", ".join(unwired))
browser_chrome = function(browser, "void FileBrowserActivity::drawChrome(")
if "if (tenorchrome::kTouchShell) tenorchrome::noteScreenTitle(basepath.c_str());" not in browser_chrome:
    raise SystemExit("RED: the X4 Pro browser bar needs the full path rather than its last folder name")

layout_struct = function(header, "struct PathPillLayout") + ";"
layout_function = function(chrome, "tenorchrome::layoutPathPill(")
draw_function = function(chrome, "tenorchrome::drawPathPill(")
note_function = function(chrome, "tenorchrome::noteScreenTitle(")

harness = r'''
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <cstdint>

struct EpdFontFamily { enum Style { REGULAR, BOLD }; };
struct Manager { uint32_t activityGeneration() const { return 1; } } activityManager;
char noted[96] = {};
uint32_t notedGeneration = 0;

static const char* nextUtf8(const char* value) {
  if (!*value) return value;
  ++value;
  while (*value && (static_cast<unsigned char>(*value) & 0xC0) == 0x80) ++value;
  return value;
}

class GfxRenderer {
 public:
  struct TextRun { int font, x, y; std::string text; bool black; EpdFontFamily::Style style; };
  mutable std::vector<TextRun> runs;
  int tier = 0;

  int getTextWidth(int, const char* value, EpdFontFamily::Style = EpdFontFamily::REGULAR) const {
    int width = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value); *p;) {
      if (*p == 0xE2 && p[1] == 0x80 && p[2] == 0xA6) {
        width += 8 + tier;
        p += 3;
      } else if (*p < 0x80) {
        width += *p == '/' ? 5 : (*p == 'i' || *p == 'l' ? 4 : 8 + tier);
        ++p;
      } else {
        width += 9 + tier;
        do { ++p; } while ((*p & 0xC0) == 0x80);
      }
    }
    return width;
  }

  std::string truncatedText(int font, const char* value, int maxWidth,
                            EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    const std::string ellipsis = "\xE2\x80\xA6";
    if (!value || maxWidth <= 0) return "";
    if (getTextWidth(font, value, style) <= maxWidth) return value;
    std::string prefix;
    for (const char* p = value; *p;) {
      const char* next = nextUtf8(p);
      std::string candidate = prefix + std::string(p, next) + ellipsis;
      if (getTextWidth(font, candidate.c_str(), style) >= maxWidth) break;
      prefix.assign(candidate.data(), candidate.size() - ellipsis.size());
      p = next;
    }
    return prefix + ellipsis;
  }

  std::array<int, 4> getTextInkBounds(int id, const char* text, EpdFontFamily::Style style) const {
    return {1, 3, getTextWidth(id, text, style), 18 + tier * 3};
  }
  int getTextInkTop(int, const char*, EpdFontFamily::Style) const { return 3; }
  int getTextInkBottom(int, const char*, EpdFontFamily::Style) const { return 18 + tier * 3; }
  int getLineHeight(int) const { return 22 + tier * 5; }
  void drawText(int font, int x, int y, const char* text, bool black = true,
                EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    runs.push_back({font, x, y, text ? text : "", black, style});
  }
};

namespace tenorchrome {
struct Ring { int x = -1, y = -1, width = -1, height = -1, thick = -1; bool grey = false; };
static Ring lastRing;
void drawPillRing(const GfxRenderer&, int x, int y, int width, int height, int thick, bool grey) {
  lastRing = {x, y, width, height, thick, grey};
}
LAYOUT_STRUCT
PathPillLayout layoutPathPill(const GfxRenderer&, int, const char*, int, int, int);
void drawPillLabel(const GfxRenderer&, int, const char*, int, int, int, int);
void drawPathPill(const GfxRenderer&, int, const char*, int, int, int, int, int, int, bool);
void noteScreenTitle(const char*);
}

LAYOUT_FUNCTION
LABEL_FUNCTION
DRAW_FUNCTION
NOTE_FUNCTION

static int failures = 0;
static void check(bool passed, const char* message) {
  if (passed) return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  ++failures;
}

int main() {
  constexpr int X4_PAD_LEFT = 18, X4_PAD_RIGHT = 22;
  constexpr int X3_PAD = 14;
  const char* path = "/Tài liệu rất dài/books/tenor";
  const char* suffix = "\xE2\x80\xA6/books/tenor";
  for (int tier = 0; tier < 3; ++tier) {
    GfxRenderer renderer;
    renderer.tier = tier;

    const auto x4Short = tenorchrome::layoutPathPill(
        renderer, 12, "Clock", 208, X4_PAD_LEFT, X4_PAD_RIGHT);
    check(x4Short.text == "Clock", "X4 Pro short title stays whole");
    check(x4Short.width == renderer.getTextWidth(12, "Clock") + X4_PAD_LEFT + X4_PAD_RIGHT,
          "X4 Pro short pill hugs rendered glyph width");

    const auto x3Short = tenorchrome::layoutPathPill(
        renderer, 10, "Clock settings", 444, X3_PAD, X3_PAD);
    check(x3Short.text == "Clock settings", "X3 short title stays whole");
    check(x3Short.width == renderer.getTextWidth(10, "Clock settings") + 2 * X3_PAD,
          "X3 short pill hugs rendered glyph width");

    const int x4LongWidth = renderer.getTextWidth(12, suffix) + X4_PAD_LEFT + X4_PAD_RIGHT + 7;
    const auto x4Long = tenorchrome::layoutPathPill(
        renderer, 12, path, x4LongWidth, X4_PAD_LEFT, X4_PAD_RIGHT);
    check(x4Long.text == suffix, "X4 Pro long path cuts left at a folder boundary");
    check(x4Long.width == x4LongWidth, "X4 Pro long path uses all available width");
    check(renderer.getTextWidth(12, x4Long.text.c_str()) + X4_PAD_LEFT + X4_PAD_RIGHT < x4Long.width,
          "X4 Pro truncated path keeps the available slack inside its full pill");

    const int x3LongWidth = renderer.getTextWidth(10, suffix) + 2 * X3_PAD + 7;
    const auto x3Long = tenorchrome::layoutPathPill(
        renderer, 10, path, x3LongWidth, X3_PAD, X3_PAD);
    check(x3Long.text == suffix, "X3 long path cuts left at a folder boundary");
    check(x3Long.width == x3LongWidth, "X3 long path uses all available width");
    check(renderer.getTextWidth(10, x3Long.text.c_str()) + 2 * X3_PAD < x3Long.width,
          "X3 truncated path keeps the available slack inside its full pill");

    const auto title = tenorchrome::layoutPathPill(
        renderer, 12, "A very long ordinary title", x4LongWidth, X4_PAD_LEFT, X4_PAD_RIGHT);
    check(title.text.rfind("\xE2\x80\xA6", 0) == 0, "every overflowing path-pill label cuts left");
    check(title.text.size() >= 5 && title.text.substr(title.text.size() - 5) == "title",
          "an overflowing screen title keeps its trailing text");

    renderer.runs.clear();
    tenorchrome::drawPathPill(renderer, 10, path, 68, 700, x3LongWidth,
                              60 + tier * (tier == 1 ? 7 : 6), X3_PAD, X3_PAD, false);
    check(tenorchrome::lastRing.x == 68 && tenorchrome::lastRing.width == x3LongWidth,
          "shared draw uses measured X3 pill width");
    check(renderer.runs.size() == 1 && renderer.runs[0].text == suffix,
          "shared draw paints the X3 left-truncated path");

    renderer.runs.clear();
    tenorchrome::drawPathPill(renderer, 12, "Clock", 256, 724, 208, 60,
                              X4_PAD_LEFT, X4_PAD_RIGHT, true);
    check(tenorchrome::lastRing.width == x4Short.width && tenorchrome::lastRing.grey,
          "shared draw uses measured X4 Pro pill width");
    check(renderer.runs.size() == 1 && renderer.runs[0].x ==
              256 + (x4Short.width - (renderer.getTextWidth(12, "Clock") - 1)) / 2 - 1,
          "shared draw centres X4 Pro label ink with a consistent left bias");
    check(renderer.runs.size() == 1 && renderer.runs[0].black &&
              renderer.runs[0].style == EpdFontFamily::REGULAR,
          "shared draw preserves the original black regular footbar title");

    const std::string fullPath = "/" + std::string(300, 'a') + "/books/tenor";
    tenorchrome::noteScreenTitle(fullPath.c_str());
    check(std::strlen(noted) < sizeof(noted) && std::string(noted).find("/books/tenor") != std::string::npos,
          "screen metadata preserves the deepest folders beyond its fixed buffer");
    const auto notedPath = tenorchrome::layoutPathPill(renderer, 12, noted, 208, X4_PAD_LEFT, X4_PAD_RIGHT);
    check(notedPath.width == 208 && notedPath.text == suffix,
          "an already clipped screen path retains its ellipsis and full bar width");
    const std::string fullTitle = std::string(300, 'a') + " Settings";
    tenorchrome::noteScreenTitle(fullTitle.c_str());
    const auto notedTitle = tenorchrome::layoutPathPill(renderer, 12, noted, 208, X4_PAD_LEFT, X4_PAD_RIGHT);
    check(notedTitle.text.size() >= 8 && notedTitle.text.substr(notedTitle.text.size() - 8) == "Settings",
          "screen metadata keeps the trailing text of every overflowing pill label");
  }
  std::printf("%s: path pill layout for 3 menu sizes, X3 and X4 Pro\n",
              failures ? "RED" : "GREEN");
  return failures ? 1 : 0;
}
'''

harness = harness.replace("LAYOUT_STRUCT", layout_struct)
harness = harness.replace("LAYOUT_FUNCTION", layout_function)
harness = harness.replace("LABEL_FUNCTION", function(chrome, "tenorchrome::drawPillLabel(")
                          if "void drawPillLabel(" in header else "")
harness = harness.replace("DRAW_FUNCTION", draw_function)
harness = harness.replace("NOTE_FUNCTION", note_function)

args.work.mkdir(parents=True, exist_ok=True)
source = args.work / "PathPillHarness.cpp"
binary = args.work / "path_pill_test"
source.write_text(harness)
subprocess.run(
    ["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(binary)],
    check=True,
)
raise SystemExit(subprocess.run([str(binary)]).returncode)
