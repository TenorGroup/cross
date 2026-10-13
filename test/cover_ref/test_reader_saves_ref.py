#!/usr/bin/env python3
"""The reader writes <book cache>/cover.ref after a book's first frame for EVERY book: Home's idle cover
step (X3) finds the cover through it. Before, only a book that still owed its thumbnails got one, and a
reader that had written them during the open never did (X3, v1.0.58 probe, 11/10/2026: 3 of 3 books
without cover.ref). Runs the production blocks of EpubReaderActivity.cpp with stubs.

  test_reader_saves_ref.py [--source src/activities/reader/EpubReaderActivity.cpp]
"""
from pathlib import Path
import argparse, subprocess, tempfile

repo = Path(__file__).resolve().parents[2]
a = argparse.ArgumentParser()
a.add_argument('--source', type=Path, default=repo / 'src/activities/reader/EpubReaderActivity.cpp')
t = a.parse_args().source.read_text()


def block(head):
    start = t.index(head)
    depth, end = 1, t.index('{', start) + 1
    while depth:
        depth += (t[end] == '{') - (t[end] == '}')
        end += 1
    return t[start:end] + '\n'


stubs = r'''
#include <cassert>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include "util/CoverRef.h"
#define LOG_DBG(...) ((void)0)
#define HEAP_MARK(...) ((void)0)
std::set<std::string> files;
int saves = 0;
bool coverref::load(const std::string& cache, std::string& href) {
  if (!files.count(cache + "/cover.ref")) return false;
  href = files.count(cache + "/other") ? "other.jpg" : "OEBPS/images/cover.jpg";
  return true;
}
bool coverref::save(const std::string& cache, const std::string&) { ++saves; files.insert(cache + "/cover.ref"); files.erase(cache + "/other"); return true; }
struct { bool exists(const char* p) { return files.count(p); } } Storage;
namespace tenorchrome { bool enabled() { return true; } }
constexpr int HOME_CARD_COVER_H = 450;
struct Metrics { int homeCoverHeight = 226; };
struct UITheme { static UITheme& getInstance() { static UITheme u; return u; } Metrics getMetrics() const { return {}; } };
struct Epub {
  std::string getCachePath() const { return "/c"; }
  const std::string& getCoverHref() const { static std::string h = "OEBPS/images/cover.jpg"; return h; }
  std::string getThumbBmpPath(int h) const { return "/c/thumb2_" + std::to_string(h) + ".bmp"; }
};
struct CoverThumbCapture { CoverThumbCapture(Epub&, int*, int) {} };
template <typename T, typename... A> std::unique_ptr<T> makeUniqueNoThrow(A&&... a) { return std::make_unique<T>(a...); }
struct ImageBlock { static void setThumbHook(void*) {} };
struct Reader {
  std::unique_ptr<Epub> epub = std::make_unique<Epub>();
  bool preview = false, coverRefPending = false, openCommitPending = true;
  int pendingThumbCount = 0, pendingThumbHeights[2] = {};
  std::unique_ptr<CoverThumbCapture> coverThumbs;
  void open() {
'''
mid = r'''
  }
  void firstFrame() {
    openCommitPending = false;
'''
end = r'''
  }
};
int main() {
  // The thumbnails were written during the open (the reader's own capture): no thumbnail is owed.
  files = {"/c/thumb2_450.bmp", "/c/thumb2_226.bmp"};
  { Reader r; r.open(); assert(saves == 0); r.firstFrame(); assert(saves == 1); }
  std::puts("GREEN a book with its thumbnails already written gets cover.ref after its first frame");
  { Reader r; r.open(); r.firstFrame(); assert(saves == 1); }
  files.insert("/c/other");
  { Reader r; r.open(); r.firstFrame(); assert(saves == 2); }
  std::puts("GREEN an equal cover.ref is not written again; one naming another cover is");
  { Reader r; r.preview = true; files.erase("/c/cover.ref"); r.open(); r.firstFrame(); assert(saves == 2); }
  files.clear();
  { Reader r; r.open(); assert(r.pendingThumbCount == 2); r.firstFrame(); assert(saves == 3); }
  std::puts("GREEN a preview writes nothing; a book still owing thumbnails gets it as before");
}
'''
src = stubs + block('  if (!preview) {\n    pendingThumbCount = 0;') + mid + block('  if (coverRefPending && !openCommitPending) {') + end
with tempfile.TemporaryDirectory(prefix='cover-ref-') as tmp:
    cpp = Path(tmp) / 'ref.cpp'
    cpp.write_text(src)
    subprocess.run(['c++', '-std=c++20', '-UNDEBUG', '-I' + str(repo / 'src'), str(cpp), '-o', str(Path(tmp) / 'ref')], check=True)
    raise SystemExit(subprocess.run([str(Path(tmp) / 'ref')]).returncode)
