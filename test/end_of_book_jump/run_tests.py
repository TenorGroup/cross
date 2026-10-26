#!/usr/bin/env python3
"""Compile the production end-of-book return, page turn, relayout and landing code on a host chapter model.

The page a reader lands on, and the progress saved there, depend on a jump left pending by the return from the end
screen (EpubReaderActivity::onReturnFromEndOfBook). The functions and the two landing slices of renderBook are taken
verbatim from the source, so a change to them changes the result.
"""
import argparse
import json
import pathlib
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("source", type=pathlib.Path)
parser.add_argument("output", type=pathlib.Path)
parser.add_argument("--compiler", default="c++")
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
epub = (args.source / "src/activities/reader/EpubReaderActivity.cpp").read_text()


def function(name):
    match = re.search(r"\bEpubReaderActivity::" + name + r"\s*\(", epub)
    if not match:
        raise ValueError("Missing production function: EpubReaderActivity::" + name)
    start = epub.rfind("\n", 0, match.start()) + 1
    brace = epub.index("{", match.end())
    depth = 1
    end = brace + 1
    while depth:
        depth += (epub[end] == "{") - (epub[end] == "}")
        end += 1
    return epub[start:end]


def slice_between(first, last):
    start = epub.index(first)
    return epub[start:epub.index(last, start) + len(last)]


# renderBook, in production order: the preview's "same place" test, how the chapter's offset target is chosen, and
# where the laid-out chapter's page is set from the jump, the offset and the preview.
same_place = slice_between("const bool samePlace =", ";")
offset_choice = slice_between("const bool explicitOffsetJump = pendingOffsetJump.has_value();", ": cachedVisibleTextOffset;")
landing = slice_between("    if (pendingPageJump.has_value()) {\n      section->currentPage = *pendingPageJump;",
                        "    pendingOffsetJump.reset();\n")

fixture = pathlib.Path(__file__).with_name("fixture.hpp").read_text()
cases = pathlib.Path(__file__).with_name("cases.cpp").read_text()
projection = args.output / "projection.cpp"
projection.write_text(fixture + "\n" + "\n\n".join(function(n) for n in ("latTrangThat", "onReturnFromEndOfBook", "danLaiTrang")) +
                      "\nvoid EpubReaderActivity::paint() {\n"
                      "  if (currentSpineIndex == epub->getSpineItemsCount()) return;  // the end screen paints no page\n"
                      "  if (xemTruoc && !section) {\n    " + same_place + "\n"
                      "    if (samePlace) pendingOffsetJump = xemTruocDich;  // the preview's chapter is laid out here\n"
                      "    xemTruoc = false;\n  }\n"
                      "  if (!section) {\n    section = layoutChapter(currentSpineIndex);\n    " + offset_choice + "\n" +
                      landing +
                      "    // The page past a chapter's end (the 65535 of a jump to the last page) is its last page (renderBook).\n"
                      "    if (section->currentPage >= static_cast<int>(section->pageCount)) section->currentPage = section->pageCount - 1;\n"
                      "  }\n"
                      "  savedSpine = currentSpineIndex;  // what the paint stores as the progress\n"
                      "  savedPage = section->currentPage;\n}\n" + cases)
command = [args.compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-fsanitize=address,undefined",
           "-g", str(projection), "-o", str(args.output / "projection")]
(args.output / "compile-command.json").write_text(json.dumps(command, indent=2))
compiled = subprocess.run(command, capture_output=True, text=True)
(args.output / "compile.log").write_text(compiled.stdout + compiled.stderr)
if compiled.returncode:
    print(compiled.stderr)
    raise SystemExit(compiled.returncode)
run = subprocess.run([str(args.output / "projection")], capture_output=True, text=True)
(args.output / "results.log").write_text(run.stdout + run.stderr)
print(run.stdout + run.stderr, end="")
raise SystemExit(run.returncode)
