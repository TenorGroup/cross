#!/usr/bin/env python3
"""Render the production F9 prefix through real UglyInk and GfxRenderer on a RAM framebuffer."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path


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
GEOMETRIES = ((528, 792), (480, 800))


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


def render_prefix(source: str) -> str:
    render = block_from(source, "void Notebook::render(RenderLock&&)")
    begin = render.index("  [[maybe_unused]]")
    end = re.search(r"^  const int perPage = .*?;$", render, re.MULTILINE)
    if not end:
        raise ValueError("render prefix no longer exposes perPage")
    prefix = render[begin : end.end()]
    prefix = prefix.replace("Size::", "ugly::Size::")
    prefix = re.sub(r"\bparagraph\(", "countedParagraph(", prefix)
    for name in ("line", "text", "width", "underline"):
        prefix = re.sub(rf"\b{name}\(", f"ugly::{name}(", prefix)
    return prefix


def constants_from(source: str) -> str:
    found = []
    for name in CONSTANTS:
        match = re.search(rf"^constexpr int {name} = [^;]+;", source, re.MULTILINE)
        if not match:
            raise ValueError(f"missing production constant {name}")
        found.append(match.group(0))
    return "\n".join(found)


def harness(source: str) -> str:
    prefix = render_prefix(source)
    constants = constants_from(source)
    return f'''#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <InlineSymbols.h>
#include <HalClock.h>
#include <HalPowerManager.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "shells/ugly/UglyInk.h"

std::vector<uint8_t> pixels(HalDisplay::BUFFER_SIZE, 0xff);
HalClock halClock;
HalPowerManager powerManager;
HalDisplay display;

HalDisplay::HalDisplay() = default;
HalDisplay::~HalDisplay() = default;
uint8_t* HalDisplay::getFrameBuffer() const {{ return pixels.data(); }}
uint16_t HalDisplay::getDisplayWidth() const {{ return DISPLAY_WIDTH; }}
uint16_t HalDisplay::getDisplayHeight() const {{ return DISPLAY_HEIGHT; }}
uint16_t HalDisplay::getDisplayWidthBytes() const {{ return DISPLAY_WIDTH_BYTES; }}
uint32_t HalDisplay::getBufferSize() const {{ return BUFFER_SIZE; }}
void HalDisplay::clearScreen(uint8_t color) const {{ std::fill(pixels.begin(), pixels.end(), color); }}
HalDisplay::Controller HalDisplay::getController() const {{ return Controller::Test; }}

namespace buttonSymbols {{
inlineSymbols::Spec resolve(int) {{ return {{inlineSymbols::Shape::None, nullptr}}; }}
}}

namespace homerows {{
enum class Page {{ Recent, Folder, Stats, Settings, Favorites }};
constexpr int PAGE_COUNT = 5;
constexpr int PAGE_TITLES[PAGE_COUNT] = {{0, 1, 2, 3, 4}};
}}

struct I18n {{ const char* get(int) const {{ return "Trang"; }} }} I18N;
static int layoutCalls = 0;
static uint64_t layoutNs = 0;

int id(homerows::Page page) {{ return static_cast<int>(page); }}
int countedParagraph(const GfxRenderer& r, ugly::Size size, int x, int baseline, int room, int step, const char* value,
                     bool draw = true) {{
  const auto started = std::chrono::steady_clock::now();
  ++layoutCalls;
  const int lines = ugly::paragraph(r, size, x, baseline, room, step, value, draw);
  layoutNs += static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count());
  return lines;
}}

{constants}

struct Harness {{
  Harness(GfxRenderer& renderer, const char* subtitle) : renderer(renderer), subtitle_(subtitle) {{}}
  int pagePosition(homerows::Page) const {{ return 0; }}
  const char* subtitle() const {{ return subtitle_; }}
  int subtitleLines() const {{
    const int room = renderer.getScreenWidth() - TEXT_X - SUBTITLE_INDENT - SUBTITLE_EDGE;
    return std::min(2, countedParagraph(renderer, ugly::Size::S30, 0, 0, room, SUBTITLE_LINE, subtitle(), false));
  }}
  int firstBaseline() const {{ return FIRST_BASELINE + (subtitleLines() - 1) * 30; }}
  int rowsPerPage() const {{
    return std::max(1, (renderer.getScreenHeight() - 140 - firstBaseline()) / ROW_HEIGHT + 1);
  }}
  int rowCount() const {{ return 12; }}
  void render();
  GfxRenderer& renderer;
  const char* subtitle_;
  homerows::Page page = homerows::Page::Recent;
  int observedFirst = 0;
  int observedPerPage = 0;
}};

void Harness::render() {{
{prefix}
  observedFirst = first;
  observedPerPage = perPage;
  (void)count;
}}

void writePbm(const char* path, const int logicalWidth, const int logicalHeight) {{
  std::ofstream out(path, std::ios::binary);
  out << "P4\\n" << logicalWidth << " " << logicalHeight << "\\n";
  std::vector<uint8_t> row(static_cast<size_t>((logicalWidth + 7) / 8));
  for (int y = 0; y < logicalHeight; ++y) {{
    std::fill(row.begin(), row.end(), 0);
    for (int x = 0; x < logicalWidth; ++x) {{
      const int phyX = y;
      const int phyY = HalDisplay::DISPLAY_HEIGHT - 1 - x;
      const bool black = !(pixels[static_cast<size_t>(phyY) * HalDisplay::DISPLAY_WIDTH_BYTES + phyX / 8] &
                           (0x80 >> (phyX % 8)));
      if (black) row[static_cast<size_t>(x / 8)] |= static_cast<uint8_t>(0x80 >> (x % 8));
    }}
    out.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
  }}
}}

int main(int argc, char** argv) {{
  if (argc != 2) return 2;
  const char* subtitles[] = {{"", "Mot dong", "Dong mot\\nDong hai", "Mot\\nHai\\nBa"}};
  HalDisplay panel;
  GfxRenderer renderer(panel);
  renderer.begin();
  renderer.setOrientation(GfxRenderer::Portrait);
  ugly::ensureFonts(renderer);
  constexpr int iterations = 5000;
  for (int kind = 0; kind < 4; ++kind) {{
    Harness notebook(renderer, subtitles[kind]);
    layoutCalls = 0;
    layoutNs = 0;
    const auto started = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) notebook.render();
    const uint64_t frameNs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count());
    char path[512];
    std::snprintf(path, sizeof(path), "%s/lines-%d.pbm", argv[1], kind);
    writePbm(path, renderer.getScreenWidth(), renderer.getScreenHeight());
    std::printf("%d %d %d %d %llu %llu\\n", kind, layoutCalls / iterations, notebook.observedFirst,
                notebook.observedPerPage, static_cast<unsigned long long>(layoutNs / iterations),
                static_cast<unsigned long long>(frameNs / iterations));
  }}
}}
'''


def compile_variant(repo: Path, source: str, width: int, height: int, build: Path, name: str) -> Path:
    cpp = build / f"{name}-{width}x{height}.cpp"
    binary = build / f"{name}-{width}x{height}"
    cpp.write_text(harness(source), encoding="utf-8")
    stubs = repo / "test/ugly_notebook_subtitle/raster_stubs"
    common_stubs = repo / "test/button_symbols/stubs"
    freeink_display = Path("/Users/tuan/Tenor/outputs/261008-v1054/home/source/freeink-sdk/libs/display/FreeInkDisplay/include")
    sources = (
        cpp,
        repo / "src/shells/ugly/UglyInk.cpp",
        repo / "lib/GfxRenderer/GfxRenderer.cpp",
        repo / "lib/GfxRenderer/FontCacheManager.cpp",
        repo / "lib/GfxRenderer/InlineSymbols.cpp",
        repo / "lib/EpdFont/EpdFont.cpp",
        repo / "lib/EpdFont/EpdFontFamily.cpp",
        repo / "lib/EpdFont/SdCardFont.cpp",
        repo / "lib/Utf8/Utf8.cpp",
        repo / "lib/Epub/Epub/blocks/TextBlock.cpp",
        repo / "lib/MiniBidi/BidiUtils.cpp",
    )
    includes = (
        stubs,
        common_stubs,
        repo / "test/sd_card_font/stubs",
        repo / "src",
        repo / "src/components",
        repo / "lib/EpdFont",
        repo / "lib/Utf8",
        repo / "lib/GfxRenderer",
        repo / "lib/Epub",
        repo / "lib/Memory",
        repo / "lib/MiniBidi",
        repo / "lib/Serialization",
        repo / "lib/hal",
        repo / "lib/I18n",
        freeink_display,
    )
    minibidi = build / f"minibidi-{width}x{height}.o"
    subprocess.run(
        ["cc", "-std=c11", "-O2", "-ffunction-sections", "-fdata-sections", "-c",
         str(repo / "lib/MiniBidi/minibidi.c"), "-o", str(minibidi)],
        check=True,
    )
    command = [
        "c++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-Wno-missing-field-initializers",
        "-ffunction-sections", "-fdata-sections", "-DFREEINK_DEVICE_X4PRO=0",
        f"-DF9_PANEL_WIDTH={height}", f"-DF9_PANEL_HEIGHT={width}",
        *(f"-I{path}" for path in includes), *(str(path) for path in sources), str(minibidi),
        "-Wl,-dead_strip", "-o", str(binary),
    ]
    subprocess.run(command, check=True)
    return binary


def run_variant(binary: Path, output: Path) -> list[dict[str, int | str]]:
    output.mkdir(parents=True, exist_ok=True)
    lines = subprocess.check_output([str(binary), str(output)], text=True).strip().splitlines()
    results = []
    for line in lines:
        kind, calls, first, per_page, layout_ns, frame_ns = map(int, line.split())
        image = output / f"lines-{kind}.pbm"
        results.append({
            "lines": kind,
            "calls": calls,
            "first": first,
            "per_page": per_page,
            "layout_host_ns": layout_ns,
            "frame_host_ns": frame_ns,
            "raster_sha256": hashlib.sha256(image.read_bytes()).hexdigest(),
        })
    return results


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--baseline-ref", default="3dc5636f")
    args = parser.parse_args()
    repo = args.repo.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    relative = "src/shells/ugly/UglyNotebook.cpp"
    before = subprocess.check_output(["git", "-C", str(repo), "show", f"{args.baseline_ref}:{relative}"], text=True)
    after = (repo / relative).read_text(encoding="utf-8")
    all_results = []
    with tempfile.TemporaryDirectory(prefix="f9-raster-") as temp:
        build = Path(temp)
        for width, height in GEOMETRIES:
            pair = {}
            for name, source in (("before", before), ("after", after)):
                binary = compile_variant(repo, source, width, height, build, name)
                pair[name] = run_variant(binary, output / name / f"{width}x{height}")
            for old, new in zip(pair["before"], pair["after"], strict=True):
                for key in ("lines", "first", "per_page", "raster_sha256"):
                    if old[key] != new[key]:
                        raise AssertionError(f"{width}x{height} {key}: {old[key]} -> {new[key]}")
                if old["calls"] != 3 or new["calls"] != 1:
                    raise AssertionError(f"{width}x{height} calls: {old['calls']} -> {new['calls']}")
                all_results.append({"width": width, "height": height, "before": old, "after": new})
    (output / "results.json").write_text(json.dumps(all_results, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(all_results, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
