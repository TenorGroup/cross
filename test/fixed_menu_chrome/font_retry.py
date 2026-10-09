from pathlib import Path
import argparse
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--work', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
source = (root / 'src/activities/settings/FontDownloadActivity.cpp').read_text()
method = source[source.index('void FontDownloadActivity::renderX3State()'):]
unit = r'''
#include <algorithm>
#include <cassert>
#include <string>
#include <vector>
struct Rect {
  int x = 16, y = 16, width = 496, height = 660;
  int bottom() const { return y + height; }
};
constexpr int SMALL_FONT_ID = 0;
enum State { LOADING_MANIFEST, DOWNLOADING, COMPLETE, ERROR };
enum Key { STR_CANCEL, STR_BACK, STR_RETRY, STR_SELECT, STR_DIR_UP, STR_DIR_DOWN };
int tier = 0, pages = 1;
const char* tr(Key key) {
  static const char* words[] = {"Cancel", "Back", "Retry", "Select", "Up", "Down"};
  return words[key];
}
struct Renderer {
  void clearScreen() {}
  void displayBuffer() {}
  int getLineHeight(int) const { return 22 + tier * 5 + (tier == 2 ? 1 : 0); }
  int getScreenWidth() const { return 528; }
};
struct Draw { std::string text; Rect rect; };
std::vector<Draw> drawings;
namespace tenorchrome {
struct Layout { Rect bodyRect; Rect barRect{16, 684 - tier * 5, 496, 72}; };
Layout x3ScreenLayout(const Renderer&) {
  Layout layout;
  layout.bodyRect.height = layout.barRect.y - 8 - layout.bodyRect.y;
  return layout;
}
void drawInfoInRect(const Renderer&, const char* text, Rect rect, int) { drawings.push_back({text, rect}); }
}
namespace x3settings {
int infoPageCount(const Renderer&, const char*, Rect) { return pages; }
void drawInfoPage(const Renderer&, const char* text, Rect rect, int) { drawings.push_back({text, rect}); }
}
struct Metrics { int progressBarHeight = 12, contentSidePadding = 16; };
struct UITheme {
  static UITheme& getInstance() { static UITheme theme; return theme; }
  Metrics getMetrics() { return {}; }
};
struct Labels { const char* btn1; const char* btn2; const char* btn3; const char* btn4; };
struct Input { Labels mapLabels(const char* a, const char* b, const char* c, const char* d) { return {a,b,c,d}; } };
struct Gui {
  std::string confirm;
  void drawProgressBar(const Renderer&, Rect rect, int, int) { drawings.push_back({"progress", rect}); }
  void drawButtonHints(const Renderer&, const char*, const char* value, const char*, const char*) { confirm = value; }
} GUI;
struct FontDownloadActivity {
  Renderer renderer;
  Input mappedInput;
  State state_ = ERROR;
  int infoPages_ = 1, infoPage_ = 0, fileTotal_ = 100, fileProgress_ = 50;
  bool batchResult_ = false, installingPack_ = false;
  unsigned batchFailureCount_ = 0;
  int downloadingFamilyIndex_ = 0;
  uint64_t familyDownloadedBytes_ = 0;
  struct Family { uint64_t totalSize = 100; };
  std::vector<Family> families_{Family{}};
  std::string x3InfoText() { return "Error details"; }
  void drawChrome() {}
  void renderX3State();
};
PRODUCTION
int main() {
  int cases = 0;
  for (tier = 0; tier < 3; ++tier) for (const int pageCount : {1, 3})
  for (const auto state : {LOADING_MANIFEST, DOWNLOADING, COMPLETE, ERROR}) {
    drawings.clear();
    pages = pageCount;
    FontDownloadActivity page;
    page.state_ = state;
    page.renderX3State();
    const auto layout = tenorchrome::x3ScreenLayout(page.renderer);
    int retries = 0;
    for (size_t index = 0; index < drawings.size(); ++index) {
      const auto& draw = drawings[index];
      assert(draw.rect.y >= layout.bodyRect.y && draw.rect.bottom() <= layout.bodyRect.bottom());
      assert(layout.barRect.y - draw.rect.bottom() >= 8);
      if (draw.text == tr(STR_RETRY)) ++retries;
      for (size_t previous = 0; previous < index; ++previous)
        assert(draw.rect.y >= drawings[previous].rect.bottom() ||
               drawings[previous].rect.y >= draw.rect.bottom());
    }
    assert(retries == (state == ERROR ? 1 : 0));
    assert(GUI.confirm == (state == ERROR ? tr(STR_SELECT) : ""));
    ++cases;
  }
  std::printf("GREEN: %d font states, retry in body, 8 px bar clearance and no overlap\n", cases);
}
'''.replace('PRODUCTION', method)
args.work.mkdir(parents=True, exist_ok=True)
path = args.work / 'font-retry.cpp'
path.write_text(unit)
program = args.work / 'font-retry'
subprocess.run(['c++', '-std=c++20', '-UNDEBUG', str(path), '-o', str(program)], check=True)
subprocess.run([str(program)], check=True)
