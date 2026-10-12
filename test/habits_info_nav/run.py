#!/usr/bin/env python3
"""X3 Reading habits: Left/Right walk the explanation's pages, then move to the next habit.

The production ReadingHabitsActivity::loop() and InfoPages are extracted verbatim and driven with a scripted
button stream; only the activity base and the input are stubbed. RED on v1.0.58: InfoPages::turn wrapped a
2-page explanation round under Right, so the list never saw a Right and the cursor stayed on the first habit.
"""
import argparse
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]


def function(source, signature):
    begin = source.index(signature)
    opening = source.index("{", begin)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[begin:end]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--compiler", default="clang++")
    a = ap.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    habits = (REPO / "src/activities/home/ReadingHabitsActivity.cpp").read_text()
    layout = (REPO / "src/activities/X3ActivityLayout.h").read_text()
    pages = layout[layout.index("class InfoPages {"):]
    pages = pages[:pages.index("  template <typename Visitor>")] + "};\n"
    (a.output / "production_pages.inc").write_text(pages)
    (a.output / "production_loop.inc").write_text(function(habits, "void ReadingHabitsActivity::loop() {"))
    binary = a.output / "habits_info_nav"
    subprocess.run([a.compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-I" + str(a.output),
                    str(HERE / "HabitsInfoNav.cpp"), "-o", str(binary)], check=True)
    result = subprocess.run([str(binary)], text=True, capture_output=True)
    print(result.stdout + result.stderr, end="")
    raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()
