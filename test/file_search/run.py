#!/usr/bin/env python3
"""Host test for the File Browser's local filename search helpers."""
import argparse
import subprocess
from pathlib import Path


def extract_function(source: str, signature: str) -> str:
    begin = source.index(signature)
    opening = source.index("{", begin)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[begin:end]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    source = args.source.read_text()
    header = args.source.with_suffix('.h').read_text()
    home_source = (args.source.parent / 'HomeActivity.cpp').read_text()
    search_rows = next((line.strip().replace('static constexpr', 'constexpr')
                        for line in header.splitlines() if 'kSearchRows =' in line), None)
    if search_rows is None:
        raise AssertionError('File search rows need one device-specific decision')
    if "searchSourceFiles" in source or "searchSourceFiles" in header:
        raise AssertionError("search must not retain a second full name list")
    if "searchIndices" not in source or "searchIndices" not in header:
        raise AssertionError("search needs a bounded index result")
    if "buildFileSearch(screen);" not in source:
        raise AssertionError("an active no-match search must keep the Search field")
    browser_callback = extract_function(source, "void FileBrowserActivity::openSearch()")
    if "requestUpdate(true)" in browser_callback:
        raise AssertionError("file search callback still requests an immediate render")
    home_callback = extract_function(home_source, "void HomeActivity::openFileSearch()")
    if "requestUpdate(true)" in home_callback:
        raise AssertionError("home file search callback still requests an immediate render")
    for touch in (False, True):
        policy = args.output / f'search_rows_{int(touch)}.cpp'
        policy.write_text(f'namespace tenorchrome {{ constexpr bool kTouchShell = {str(touch).lower()}; }}\n'
                          + search_rows + '\nstatic_assert(kSearchRows == 0);\n')
        subprocess.run(['clang++', '-std=c++20', '-fsyntax-only', str(policy)], check=True)
    browser_entry = extract_function(source, "const std::string& FileBrowserActivity::entryForVisibleRow")
    browser_path = extract_function(source, "std::string FileBrowserActivity::fullPathForVisibleRow")
    home_entry = extract_function(home_source, "const std::string& HomeActivity::folderEntry")
    home_path = extract_function(home_source, "std::string HomeActivity::folderFullPath")
    home_opens = extract_function(home_source, "bool HomeActivity::rowOpens(")
    for signature in ("void FileBrowserActivity::activateSelected()", "void FileBrowserActivity::deleteSelected()",
                      "void FileBrowserActivity::startRename()"):
        assert "fullPathForVisibleRow(nav.selected)" in extract_function(source, signature)
    assert "index >= listCount()" in extract_function(home_source, "void HomeActivity::activateIndex(")
    harness = args.output / "paths.cpp"
    harness.write_text(r'''#include "FileSearchScan.h"
#include <cassert>
namespace tenorchrome { constexpr bool kTouchShell = true; }
class FileBrowserActivity {
 public:
  static constexpr int kSearchRows=0;
  std::string basepath="/books",searchQuery; mutable std::string searchEntry;
  std::vector<std::string> files;
  filesearch::IndexBuffer searchIndices;
  bool recursiveSearch=false;
  std::unique_ptr<filesearch::Scanner> searchScanner;
  const std::string& entryForVisibleRow(int row) const;
  std::string fullPathForVisibleRow(int row) const;
};
class HomeActivity {
 public:
  enum class Tab {FOLDER, STATS, CAI_DAT};
  Tab activeTabId=Tab::FOLDER;
  std::vector<std::string> rowLabels;
  int listCount() const {return 0;}
  bool statsTurnRow(int) const {return false;}
  bool rowOpens(int row) const;
  std::string folderSearchQuery; mutable std::string folderSearchEntry;
  std::vector<std::string> mucTheNho;
  filesearch::IndexBuffer folderRowIndices;
  bool folderRecursiveSearch=false;
  std::unique_ptr<filesearch::Scanner> folderSearchScanner;
  const std::string& folderEntry(int row) const;
  std::string folderFullPath(int row) const;
};
''' + browser_entry + browser_path + home_entry + home_path + home_opens + r'''
int main() {
 FileBrowserActivity b;
 b.files={"Folder/","Z.epub","Đường 10.epub","Đường 2.epub"}; b.searchQuery="duong";
 assert(filesearch::buildResults(b.files,b.searchQuery,b.searchIndices));
 assert(b.fullPathForVisibleRow(0)=="/books/Đường 2.epub");
 assert(b.fullPathForVisibleRow(1)=="/books/Đường 10.epub");
 HomeActivity h; h.mucTheNho=b.files;h.folderSearchQuery=b.searchQuery;
 assert(filesearch::buildResults(h.mucTheNho,h.folderSearchQuery,h.folderRowIndices));
 assert(h.folderFullPath(1)=="/Đường 10.epub");
 b.searchQuery.clear();b.searchIndices.clear();
 for(size_t i=0;i<b.files.size();++i)assert(b.fullPathForVisibleRow(i)=="/books/"+b.files[i]);
 h.folderSearchQuery.clear();assert(filesearch::buildResults(h.mucTheNho,"",h.folderRowIndices));
 for(size_t i=0;i<h.mucTheNho.size();++i)assert(h.folderFullPath(i)=="/"+h.mucTheNho[i]);
 h.rowLabels=h.mucTheNho;assert(h.rowOpens(0));assert(!h.rowOpens(1));
}
''')
    binary = args.output / "paths"
    subprocess.run(['clang++', '-std=c++20', '-fsanitize=address,undefined',
                    '-I'+str(args.repo/'src/components'), '-I'+str(args.repo/'lib/Memory'),
                    '-I'+str(args.repo/'lib/LibraryIndex'), '-I'+str(args.repo/'lib/Utf8'),
                    str(harness), str(args.repo/'src/components/FileSearch.cpp'),
                    str(args.repo/'src/components/FileSearchScan.cpp'),
                    str(args.repo/'lib/LibraryIndex/LibraryText.cpp'), str(args.repo/'lib/Utf8/Utf8.cpp'),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    print('search rows: X3/X4=0, X4 Pro=0; separate Search field PASS')


if __name__ == "__main__":
    main()
