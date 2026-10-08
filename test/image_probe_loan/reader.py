#!/usr/bin/env python3
"""Compile the production frame invalidation guard and audit every background build scope."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("source", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()

reader = (args.source / "src/activities/reader/EpubReaderActivity.cpp").read_text()


def function(signature):
    start = reader.index(signature)
    brace = reader.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (reader[end] == "{") - (reader[end] == "}")
        end += 1
    return reader[start:end]


guard = "BackgroundBuildFrameGuard frameGuard(*this);"
for signature in (
    "void EpubReaderActivity::prepareNextChapter()",
    "void EpubReaderActivity::runIndexStep()",
    "void EpubReaderActivity::suspendBackgroundBuild()",
    "void EpubReaderActivity::catchUpTick(",
):
    if guard not in function(signature):
        raise SystemExit(f"missing loan guard in {signature}")

loop = function("void EpubReaderActivity::loop()")
if loop.count(guard) != 2:
    raise SystemExit(f"expected deferred and look-ahead guards in loop, found {loop.count(guard)}")

functions = "\n\n".join(
    function(signature)
    for signature in (
        "EpubReaderActivity::BackgroundBuildFrameGuard::BackgroundBuildFrameGuard(",
        "EpubReaderActivity::BackgroundBuildFrameGuard::~BackgroundBuildFrameGuard()",
        "void EpubReaderActivity::invalidatePageFrameAfterLoan(",
    )
)

fixture = r'''
#include <atomic>
#include <cstdint>
#include <iostream>

struct Renderer {
  uint32_t loans = 0;
  uint32_t frameBufferLoanCount() const { return loans; }
};

class EpubReaderActivity {
 public:
  Renderer renderer;
  std::atomic<bool> pageFrameShown{false};
  unsigned repaintRequests = 0;
  void requestUpdate() { ++repaintRequests; }

  class BackgroundBuildFrameGuard {
   public:
    explicit BackgroundBuildFrameGuard(EpubReaderActivity& activity);
    ~BackgroundBuildFrameGuard();
    BackgroundBuildFrameGuard(const BackgroundBuildFrameGuard&) = delete;
    BackgroundBuildFrameGuard& operator=(const BackgroundBuildFrameGuard&) = delete;

   private:
    EpubReaderActivity& activity;
    uint32_t loanCount;
  };
  void invalidatePageFrameAfterLoan(uint32_t loanCount);
};

@@FUNCTIONS@@

int main() {
  unsigned passed = 0;
  auto check = [&](bool condition, const char* name) {
    std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
    passed += condition;
  };
  EpubReaderActivity unchanged;
  unchanged.pageFrameShown = true;
  { EpubReaderActivity::BackgroundBuildFrameGuard guard(unchanged); }
  check(unchanged.pageFrameShown && unchanged.repaintRequests == 0,
        "no loan preserves the shown page");

  EpubReaderActivity hidden;
  { EpubReaderActivity::BackgroundBuildFrameGuard guard(hidden); ++hidden.renderer.loans; }
  check(!hidden.pageFrameShown && hidden.repaintRequests == 0,
        "a hidden page does not queue a redundant repaint");

  EpubReaderActivity shown;
  shown.pageFrameShown = true;
  { EpubReaderActivity::BackgroundBuildFrameGuard guard(shown); ++shown.renderer.loans; }
  check(!shown.pageFrameShown && shown.repaintRequests == 1,
        "a loan invalidates the shown page and queues one repaint");

  std::cout << passed << "/3 reader frame-loan paths pass\n";
  return passed == 3 ? 0 : 1;
}
'''

args.output.mkdir(parents=True, exist_ok=True)
projected = args.output / "reader.cpp"
projected.write_text(fixture.replace("@@FUNCTIONS@@", functions))
binary = args.output / "reader"
subprocess.run(
    ["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
     "-g", str(projected), "-o", str(binary)],
    check=True,
)
raise SystemExit(subprocess.run([str(binary)]).returncode)
