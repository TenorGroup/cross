"""The X4 Pro Contents sheet marks the row of the sub-chapter on the page, as the chapter list does (#3946),
not the first row of the chapter file. Compiles the production currentTocIndex() and the sheet's own block."""
import argparse
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
parser.add_argument('--output', type=Path, required=True)
a = parser.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
text = (a.repo / 'src/activities/reader/EpubReaderActivity.cpp').read_text()


def body(start):
    brace = text.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


current = body(text.index('int EpubReaderActivity::currentTocIndex() const'))
head = 'if (overlay == Overlay::Contents && !ReaderToolbarUi::usesX3Bar())'
sheet = body(text.index(head))[len(head):]
cpp = r'''
#include <cassert>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>
enum class StrId { STR_TOOL_CONTENTS };
#define tr(id) #id
struct ReaderToolbarUi { static bool usesX3Bar() { return false; } };
struct TocItem { std::string title; int level = 1; };
struct Epub {  // spine 3 holds the TOC rows 2, 3 and 4 (a chapter file with sub-chapters); row 1 is spine 2
  int getTocItemsCount() const { return 6; }
  TocItem getTocItem(int i) const { return {"row " + std::to_string(i), 1}; }
  int getTocIndexForSpineIndex(int spine) const { return spine == 3 ? 2 : spine == 2 ? 1 : -1; }
};
struct Section {
  int currentPage = 0;
  int getTocIndexForPage(int page) const { return page < 4 ? 2 : page < 9 ? 3 : 4; }
};
struct Model {
  const char* panelTitle = nullptr;
  int itemCount = 0;
  std::function<bool(int)> rowMarked;
  std::function<std::string(int)> rowText;
};
struct EpubReaderActivity {
  enum class Overlay { Contents } overlay = Overlay::Contents;
  std::shared_ptr<Epub> epub = std::make_shared<Epub>();
  std::unique_ptr<Section> section;
  int currentSpineIndex = 3;
  int currentTocIndex() const;
  Model buildContents() {
    Model model;
    ''' + 'if (overlay == Overlay::Contents && !ReaderToolbarUi::usesX3Bar())' + sheet + r'''
    return model;
  }
};
''' + current.replace('int EpubReaderActivity::currentTocIndex', 'int EpubReaderActivity::currentTocIndex') + r'''
static int marked(EpubReaderActivity& a) {
  const Model m = a.buildContents();
  int found = -1;
  for (int i = 0; i < m.itemCount; ++i)
    if (m.rowMarked(i)) { if (found >= 0) return -2; found = i; }
  return found;
}
int main() {
  EpubReaderActivity a;
  bool ok = true;
  auto expect = [&ok](const char* what, int got, int want) {
    if (got != want) { std::fprintf(stderr, "RED %s: row %d marked, want %d\n", what, got, want); ok = false; }
  };
  expect("no section yet: the chapter file's row", marked(a), 2);
  a.section = std::make_unique<Section>();
  a.section->currentPage = 1;
  expect("first sub-chapter", marked(a), 2);
  a.section->currentPage = 5;
  expect("second sub-chapter", marked(a), 3);
  a.section->currentPage = 12;
  expect("third sub-chapter", marked(a), 4);
  a.currentSpineIndex = 0;
  a.section.reset();
  expect("outside the table of contents", marked(a), -1);
  return ok ? 0 : 1;
}
'''
src = a.output / 'contents_mark.cpp'
src.write_text(cpp)
exe = a.output / 'contents_mark'
build = subprocess.run(['c++', '-std=c++20', '-g', '-fsanitize=address,undefined', str(src), '-o', str(exe)],
                       capture_output=True, text=True)
if build.returncode:
    print(build.stderr)
    raise SystemExit(build.returncode)
run = subprocess.run([str(exe)], capture_output=True, text=True)
print(run.stderr, end='')
if run.returncode:
    raise SystemExit(1)
print('GREEN: the X4 Pro Contents sheet marks the sub-chapter on the page')
