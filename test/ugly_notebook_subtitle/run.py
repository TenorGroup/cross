#!/usr/bin/env python3
"""Compile the production notebook render prefix with host stubs and count subtitle layouts."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import tempfile
from pathlib import Path


METHODS = ("subtitleLines", "firstBaseline", "rowsPerPage")
CONSTANTS = (
    "FIRST_BASELINE",
    "ROW_HEIGHT",
    "TEXT_X",
    "MARGIN_X",
    "SUBTITLE_BASELINE",
    "SUBTITLE_LINE",
    "SUBTITLE_INDENT",
    "SUBTITLE_EDGE",
)
CASES = tuple((width, height, raw_lines) for width, height in ((528, 792), (480, 800)) for raw_lines in range(4))


def block_from(text: str, marker: str) -> str:
    start = text.index(marker)
    brace = text.index("{", start)
    depth = 0
    for pos in range(brace, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                return text[start : pos + 1]
    raise ValueError(f"unterminated block: {marker}")


def production_fragments(source: str) -> tuple[str, str]:
    methods = []
    for name in METHODS:
        block = block_from(source, f"int Notebook::{name}(")
        methods.append(block.replace(f"Notebook::{name}", f"Harness::{name}", 1))

    render = block_from(source, "void Notebook::render(RenderLock&&)")
    begin = render.index("  [[maybe_unused]]")
    match = re.search(r"^  const int perPage = .*?;$", render, re.MULTILINE)
    if not match:
        raise ValueError("render prefix no longer exposes perPage")
    prefix = render[begin : match.end()]
    return "\n\n".join(methods), prefix


def constants_from(source: str) -> str:
    lines = []
    for name in CONSTANTS:
        match = re.search(rf"^constexpr int {name} = [^;]+;", source, re.MULTILINE)
        if not match:
            raise ValueError(f"missing production constant {name}")
        lines.append(match.group(0))
    return "\n".join(lines)


def harness(source: str) -> str:
    methods, render_prefix = production_fragments(source)
    constants = constants_from(source)
    return f'''#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

#define FREEINK_DEVICE_X4PRO 0

enum class Size {{ S22, S30, S52 }};
namespace homerows {{
enum class Page {{ Recent, Folder, Stats, Settings, Favorites }};
constexpr int PAGE_COUNT = 5;
constexpr int PAGE_TITLES[PAGE_COUNT] = {{0, 1, 2, 3, 4}};
}}

struct Renderer {{
  int width = 528;
  int height = 792;
  void clearScreen() {{ events += "clear;"; }}
  int getScreenWidth() const {{ return width; }}
  int getScreenHeight() const {{ return height; }}
  mutable std::string events;
}};

struct I18n {{ const char* get(int) const {{ return "title"; }} }} I18N;
static int rawLines = 1;
static int paragraphCalls = 0;
static int drawCalls = 0;
static int measureCalls = 0;

uint32_t millis() {{ return 7; }}
int id(homerows::Page page) {{ return static_cast<int>(page); }}
void line(Renderer& r, int x1, int y1, int x2, int y2, int seed, int width = 1) {{
  r.events += "line:" + std::to_string(x1) + "," + std::to_string(y1) + "," + std::to_string(x2) + "," +
              std::to_string(y2) + "," + std::to_string(seed) + "," + std::to_string(width) + ";";
}}
int text(Renderer& r, Size, int x, int y, const char* value) {{
  r.events += "text:" + std::to_string(x) + "," + std::to_string(y) + "," + value + ";";
  return static_cast<int>(std::strlen(value)) * 9;
}}
void underline(Renderer& r, int left, int right, int y, int seed, int width) {{
  r.events += "under:" + std::to_string(left) + "," + std::to_string(right) + "," + std::to_string(y) + "," +
              std::to_string(seed) + "," + std::to_string(width) + ";";
}}
int width(const Renderer&, Size, const char* value) {{ return static_cast<int>(std::strlen(value)) * 9; }}
int paragraph(const Renderer& r, Size, int x, int baseline, int room, int step, const char* value, bool draw = true) {{
  ++paragraphCalls;
  if (draw) {{
    ++drawCalls;
    r.events += "paragraph:" + std::to_string(x) + "," + std::to_string(baseline) + "," + std::to_string(room) +
                "," + std::to_string(step) + "," + value + ";";
  }} else {{
    ++measureCalls;
  }}
  return rawLines;
}}

{constants}

struct Harness {{
  explicit Harness(int width, int height) {{ renderer.width = width; renderer.height = height; }}
  int subtitleLines() const;
  int firstBaseline() const;
  int rowsPerPage() const;
  int pagePosition(homerows::Page) const {{ return 0; }}
  const char* subtitle() const {{ return "subtitle"; }}
  int rowCount() const {{ return 12; }}
  void render();
  Renderer renderer;
  homerows::Page page = homerows::Page::Recent;
  int observedFirst = 0;
  int observedPerPage = 0;
}};

{methods}

void Harness::render() {{
{render_prefix}
  observedFirst = first;
  observedPerPage = perPage;
  (void)count;
}}

uint64_t fnv1a(const std::string& value) {{
  uint64_t hash = 1469598103934665603ULL;
  for (const unsigned char byte : value) {{ hash ^= byte; hash *= 1099511628211ULL; }}
  return hash;
}}

int main(int argc, char** argv) {{
  if (argc != 4) return 2;
  Harness notebook(std::stoi(argv[1]), std::stoi(argv[2]));
  rawLines = std::stoi(argv[3]);
  notebook.render();
  std::cout << paragraphCalls << " " << drawCalls << " " << measureCalls << " " << notebook.observedFirst << " "
            << notebook.observedPerPage << " " << fnv1a(notebook.renderer.events) << "\\n";
}}
'''


def compile_and_run(repo: Path, repeat_measurement: bool = False) -> list[dict[str, int]]:
    source_path = repo / "src/shells/ugly/UglyNotebook.cpp"
    source = source_path.read_text(encoding="utf-8")
    if repeat_measurement:
        old = "const int perPage = std::max(1, (h - 140 - first) / ROW_HEIGHT + 1);"
        if source.count(old) != 1:
            raise AssertionError("mutation target must match the production render exactly once")
        source = source.replace(old, "const int perPage = rowsPerPage();")
    generated = harness(source)
    with tempfile.TemporaryDirectory(prefix="f9-subtitle-") as temp:
        temp_path = Path(temp)
        cpp = temp_path / "production_prefix.cpp"
        binary = temp_path / "production_prefix"
        cpp.write_text(generated, encoding="utf-8")
        subprocess.run(
            ["c++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(binary)],
            check=True,
        )
        results = []
        for width, height, raw_lines in CASES:
            fields = subprocess.check_output(
                [str(binary), str(width), str(height), str(raw_lines)], text=True
            ).strip().split()
            calls, draws, measures, first, per_page, digest = map(int, fields)
            results.append(
                {
                    "width": width,
                    "height": height,
                    "raw_lines": raw_lines,
                    "calls": calls,
                    "draws": draws,
                    "measures": measures,
                    "first": first,
                    "per_page": per_page,
                    "draw_trace_digest": digest,
                }
            )
        return results


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--write-baseline", type=Path)
    parser.add_argument("--compare", type=Path)
    parser.add_argument("--expect-calls", type=int)
    parser.add_argument("--check-mutation", action="store_true")
    args = parser.parse_args()

    results = compile_and_run(args.repo.resolve())
    if args.write_baseline:
        args.write_baseline.parent.mkdir(parents=True, exist_ok=True)
        args.write_baseline.write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    if args.compare:
        baseline = json.loads(args.compare.read_text(encoding="utf-8"))
        for before, after in zip(baseline, results, strict=True):
            for key in ("width", "height", "raw_lines", "draws", "first", "per_page", "draw_trace_digest"):
                if before[key] != after[key]:
                    raise AssertionError(f"{key} changed: {before[key]} -> {after[key]}")
    if args.expect_calls is not None:
        for result in results:
            if result["calls"] != args.expect_calls:
                raise AssertionError(f"expected {args.expect_calls} paragraph calls, got {result}")
    if args.check_mutation:
        mutated = compile_and_run(args.repo.resolve(), repeat_measurement=True)
        for good, bad in zip(results, mutated, strict=True):
            if bad["calls"] <= good["calls"]:
                raise AssertionError(f"repeat-measurement mutation escaped: good={good}, bad={bad}")
        print("MUTATION_CAUGHT: rowsPerPage() adds a subtitle measurement")
    print(json.dumps(results, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
