"""Run the Home loader with missing books, and check every per-book slot."""
import argparse
from pathlib import Path
import re
import subprocess

repo = Path(__file__).resolve().parents[2]
ap = argparse.ArgumentParser()
ap.add_argument('--output', type=Path, required=True)
a = ap.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
header = (repo / 'src/activities/home/HomeActivity.h').read_text()


def body(path, signature):
    text = (repo / path).read_text()
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


slots = '\n'.join(re.search(pattern, header).group() for pattern in (
    r'static constexpr size_t RECENT_LIMIT = \d+;', r'uint64_t cardQuotes\[RECENT_LIMIT\] = \{\};',
    r'BookReadingRecord cardRecords\[RECENT_LIMIT\];', r'struct CoverSize \{[^}]+\};',
    r'CoverSize cardCoverSizes\[RECENT_LIMIT\];'))
record = re.search(r'struct BookReadingRecord \{.*?\n\};',
                   (repo / 'src/ReadingStatsStore.h').read_text(), re.S).group()
source = '''#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
struct RecentBook { int id; bool missing; };
struct RecentBooksStore { static bool isMissing(const RecentBook& b) { return b.missing; } };
struct Store {
  std::vector<RecentBook> books;
  const auto& getBooks() { return books; }
} RECENT_BOOKS;
namespace homerows {
''' + body('src/activities/home/HomeRows.cpp', 'std::vector<RecentBook> recent(') + '\n}\n' + record + '''
struct HomeActivity {
''' + slots + '''
  std::vector<RecentBook> recentBooks;
  uint8_t cardCoverSizesRead = 255;
  void loadRecentBooks();
};
''' + body('src/activities/home/HomeActivity.cpp', 'void HomeActivity::loadRecentBooks()') + '''
int main() {
  HomeActivity h;
  for (int i = 0; i < 10; ++i) RECENT_BOOKS.books.push_back({i, i == 1 || i == 3});
  h.loadRecentBooks();
  std::printf("recent=%zu fixed_slots=%zu record=%zu\\n", h.recentBooks.size(),
      sizeof(h.cardQuotes) + sizeof(h.cardRecords) + sizeof(h.cardCoverSizes), sizeof(BookReadingRecord));
  std::fflush(stdout);
  assert(h.recentBooks.size() == 7);
  const int expected[] = {0, 2, 4, 5, 6, 7, 8};
  uint8_t seen = 0;
  for (size_t i = 0; i < h.recentBooks.size(); ++i) {
    assert(h.recentBooks[i].id == expected[i]);
    h.cardQuotes[i] = 100 + i;
    h.cardRecords[i].turns = 10 + i;
    h.cardCoverSizes[i] = {int(i + 1), int(i + 2)};
    seen |= uint8_t(1u << i);
  }
  assert(seen == 127 && h.cardCoverSizesRead == 0);
  RECENT_BOOKS.books.clear();
  h.loadRecentBooks();
  assert(h.recentBooks.empty());
  std::puts("GREEN: 7 valid books in source order, all slots addressable");
}
'''
path = a.output / 'recent.cpp'
path.write_text(source)
exe = a.output / 'recent'
subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                '-fsanitize=address,undefined', str(path), '-o', str(exe)], check=True)
raise SystemExit(subprocess.run([str(exe)]).returncode)
