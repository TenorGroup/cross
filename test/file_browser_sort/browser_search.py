"""Run the browser's directory read and local search against sized card entries."""
import argparse
from pathlib import Path
import subprocess

repo = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--source', type=Path, default=repo / 'src/activities/home/FileBrowserActivity.cpp')
parser.add_argument('--require-search-size', action='store_true')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
text = args.source.read_text()


def function(signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end] + '\n'


source = r'''
#include "DocThuMuc.h"
#include "FileSearch.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include "HalStorage.h"
#define LOG_ERR(...) ((void)0)
#define LOG_INF(...) ((void)0)
struct { bool showHiddenFiles = false; } SETTINGS;
struct {
  size_t getFreeHeap() const { return 45000; }
  size_t getMaxAllocHeap() const { return 20000; }
} ESP;
class FileBrowserActivity {
 public:
  enum class Mode { Books, PickFirmware };
  Mode mode = Mode::Books;
  bool recursiveSearch = false, searchIncomplete = false, tooMany = false;
  int prewarmedStart = -1;
  static constexpr size_t NAME_BUFFER_SIZE = 500;
  std::unique_ptr<char[]> fileNameBuffer{new char[NAME_BUFFER_SIZE]};
  std::string basepath = "/books", searchQuery;
  std::vector<std::string> files;
  filesearch::IndexBuffer searchIndices;
  filebrowser::SortMode sortMode = filebrowser::SortMode::Size;
  void stopRecursiveSearch() {}
  void startRecursiveSearch(const std::string&) { assert(false); }
  void loadFiles();
  void applySearch(const std::string& query);
};
'''
source += function('void FileBrowserActivity::loadFiles()')
source += function('void FileBrowserActivity::applySearch(')
source += r'''
int main(int argc, char**) {
  fakecard::folder = "/books";
  fakecard::entries = {{"book 2.epub", false, 30, 200},
                       {"book 1.epub", false, 10, 100},
                       {"book 3.epub", false, 20, 300}};
  FileBrowserActivity browser;
  browser.loadFiles();
  for (const auto& name : browser.files) std::printf("directory=%s\n", name.c_str());
  std::fflush(stdout);
  assert((browser.files == std::vector<std::string>{"book 3.epub", "book 2.epub", "book 1.epub"}));
  browser.searchQuery = "book";
  browser.applySearch(browser.searchQuery);
  assert(!browser.searchIncomplete);
  assert(browser.searchIndices.size() == 3);
  bool sameOrder = true;
  for (size_t i = 0; i < browser.searchIndices.size(); ++i) {
    std::printf("search=%s index=%u\n", browser.files[browser.searchIndices[i]].c_str(), browser.searchIndices[i]);
    if (browser.searchIndices[i] != i) sameOrder = false;
  }
  if (argc > 1 && !sameOrder) {
    std::puts("RED: local search reorders the size-sorted directory");
    return 1;
  }
  browser.searchQuery.clear();
  browser.applySearch(browser.searchQuery);
  assert(browser.searchIndices.size() == 0);
  assert(browser.files.front() == "book 3.epub");
}
'''
generated = args.output / 'browser.cpp'
generated.write_text(source)
includes = ['test/file_browser_sort/stubs', 'test/fs_helpers/stubs', 'src/activities/home',
            'src/components', 'lib/Memory', 'lib/FsHelpers', 'lib/LibraryIndex', 'lib/Utf8']
units = ['src/activities/home/DocThuMuc.cpp', 'src/activities/home/FileBrowserSort.cpp',
         'src/components/FileSearch.cpp', 'lib/FsHelpers/FsHelpers.cpp',
         'lib/LibraryIndex/LibraryText.cpp', 'lib/Utf8/Utf8.cpp']
binary = args.output / 'browser'
subprocess.run(['clang++', '-std=c++20', '-fsanitize=address,undefined',
                *['-I' + str(repo / path) for path in includes], str(generated),
                *[str(repo / path) for path in units], '-o', str(binary)], check=True)
subprocess.run([str(binary)] + (['require-search-size'] if args.require_search_size else []), check=True)
