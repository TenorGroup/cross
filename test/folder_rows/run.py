#!/usr/bin/env python3
"""Compile the production row provider/screen with the actual SDK list renderer.

Only display/font I/O and activity wiring are stubbed. The source functions are
extracted verbatim. Since #3600 the SDK list pulls each drawn row through
FileBrowserActivity::provideRow(), so nothing per file is materialized.
"""
import argparse
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]


def function(source, signature):
    begin = source.index(signature)
    opening = source.index("{", begin)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[begin:end]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=REPO / "src/activities/home/FileBrowserActivity.cpp")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--compiler", default="clang++")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    source = args.source.read_text()
    signatures = [
        "void formatFileName(const std::string& filename, char* buffer, const size_t bufferSize) {",
        "void formatFileExtension(const std::string& filename, char* buffer, const size_t bufferSize) {",
        "std::string getFileExtension(const std::string& filename) {",
        "void FileBrowserActivity::provideRow(",
        "void FileBrowserActivity::prewarmRowGlyphs(",
        "void FileBrowserActivity::buildScreen(UiScreen& screen) {",
    ]
    functions = "\n\n".join(function(source, sig) for sig in signatures)
    base = (REPO / "src/activities/UiListActivity.cpp").read_text()
    pin_state = base[base.index("struct PinDecoration {"):base.index("}  // namespace")]
    base_methods = "\n\n".join(function(base, sig) for sig in [
        "void UiListActivity::frameRows(",
        "UiListActivity::RowFrameLines UiListActivity::rowFrameLines(",
        "void UiListActivity::reserveRowFrame(",
        "void UiListActivity::syncListViewport(",
        "void UiListActivity::decoratePinnedRows(",
    ]).replace("UiListActivity::", "FileBrowserActivity::")
    chrome = (REPO / "src/components/TenorMenuChrome.h").read_text()
    start = chrome.index("#if defined(FREEINK_DEVICE_X4PRO)")
    shell = chrome[start:chrome.index("#endif", start) + len("#endif")]
    foot_x = next(line for line in chrome.splitlines() if line.startswith("constexpr int FOOT_BACK_X"))
    browser_header = (REPO / "src/activities/home/FileBrowserActivity.h").read_text()
    row_opens = function(browser_header, "bool rowOpens(int row) const override").replace(
        "bool rowOpens(int row) const override", "bool FileBrowserActivity::rowOpens(int row) const")
    base_header = (REPO / "src/activities/UiListActivity.h").read_text()
    framed = function(base_header, "virtual bool listFramed() const").replace(
        "virtual bool listFramed() const", "bool FileBrowserActivity::listFramed() const")
    generated = args.output / "production_rows.inc"
    generated.write_text("namespace tenorchrome {\n" + shell + "\n" + foot_x +
                         "\ninline bool enabled() { return true; }  // a board with buttons\n}\n" +
                         framed + "\n" + row_opens + "\n" + pin_state + "\n" + base_methods + "\n" + functions)
    binary = args.output / "folder_rows"
    cmd = [args.compiler, "-std=c++20", "-O1", "-g", "-Wall", "-Wextra",
           "-Wno-unused-parameter",
           "-I" + str(args.output), "-I" + str(REPO / "freeink-sdk/libs/ui/FreeInkUI/include"),
           "-I" + str(REPO / "src"),
           "-I" + str(REPO / "freeink-sdk/libs/assets/Icons/include"),
           "-I" + str(REPO / "lib/Utf8"), "-I" + str(REPO / "lib/FsHelpers"),
           "-I" + str(REPO / "test/host_stubs"),
           str(HERE / "FolderRowsRegression.cpp"), str(REPO / "lib/Utf8/Utf8.cpp"),
           str(REPO / "lib/FsHelpers/FsHelpers.cpp"),
           str(REPO / "freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp"), "-o", str(binary)]
    if args.sanitize:
        cmd[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(cmd, check=True)
    result = subprocess.run([str(binary)], text=True, capture_output=True)
    (args.output / "results.log").write_text(result.stdout + result.stderr)
    print(result.stdout + result.stderr, end="")
    raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()
