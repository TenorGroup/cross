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
    if "if (!searchSourceFiles.empty()) files = searchSourceFiles;" not in source:
        raise AssertionError("empty search must preserve the current directory")
    if "if (files.empty() && searchQuery.empty())" not in source:
        raise AssertionError("an active no-match search must keep the Search row")
    helpers = "\n\n".join(
        extract_function(source, signature)
        for signature in (
            "std::string fileSearchStem(const std::string& entry) {",
            "bool fileSearchMatches(const std::string& entry, const std::string& query) {",
        )
    )
    harness = f'''#include <LibraryText.h>
#include <cstdio>
#include <string>

{helpers}

int main() {{
  if (fileSearchStem("Đọc Sách.epub") != "Đọc Sách") return 1;
  if (fileSearchStem("Thư mục/") != "Thư mục") return 2;
  if (fileSearchStem("v1.0/") != "v1.0") return 8;
  if (!fileSearchMatches("Đọc Sách.epub", "doc sach")) return 3;
  if (!fileSearchMatches("Đọc Sách.epub", "DOC")) return 4;
  if (fileSearchMatches("/nested/Đọc Sách.epub", "doc")) return 5;
  if (fileSearchMatches("Đọc Sách.epub", "epub")) return 6;
  if (fileSearchMatches("Đọc Sách.epub", "khong co")) return 7;
  std::puts("file search helpers: PASS");
  return 0;
}}
'''
    harness_path = args.output / "file_search_harness.cpp"
    harness_path.write_text(harness)
    binary = args.output / "file_search_harness"
    subprocess.run(
        [
            "clang++",
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-I" + str(args.repo / "lib/LibraryIndex"),
            "-I" + str(args.repo / "lib/Utf8"),
            str(harness_path),
            str(args.repo / "lib/LibraryIndex/LibraryText.cpp"),
            str(args.repo / "lib/Utf8/Utf8.cpp"),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
