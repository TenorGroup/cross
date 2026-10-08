#!/usr/bin/env python3
"""A5: keep the reader sheet off the owning font-family catalog.

Run with REPO, --baseline for git HEAD, or --mutation to restore the copy
in memory. Both negative controls must exit nonzero. The default also runs
production catalog helpers with host boundaries, without device tooling.
"""
import argparse
import subprocess
import tempfile
from pathlib import Path


CPP = "src/activities/reader/EpubReaderActivity.cpp"
HEADER = "src/activities/reader/EpubReaderActivity.h"


def source_at(repo: Path, path: str, baseline: bool) -> str:
    if not baseline:
        return (repo / path).read_text()
    return subprocess.check_output(["git", "show", "HEAD:" + path], cwd=repo, text=True)


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for end in range(brace, len(source)):
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1:end]
    raise AssertionError("unterminated function: " + signature)


def static_contract(cpp: str, header: str) -> None:
    enter = function_body(cpp, "void EpubReaderActivity::enterFontLevel()")
    assert "fontFamilies" not in header, "fontFamilies member remains"
    assert "danhSachHo" not in enter, "enterFontLevel still copies the family vector"
    assert "fontFamilies" not in cpp, "fontFamilies use remains"


def compiled_catalog_check(repo: Path) -> None:
    source = (repo / "src/ReaderFontChon.cpp").read_text()
    begin = source.index("namespace fontdoc {")
    end = source.index("int hoDangDung", begin)
    # Compile the production declarations and complete catalog helper bodies.
    header = (repo / "src/ReaderFontChon.h").read_text()
    header = header.replace("#pragma once", "").replace("#include <SdCardFontRegistry.h>", "")
    harness = r'''#include <cassert>
#include <climits>
#include <cstdint>
#include <string>
#include <vector>

enum class StrId { STR_NOTO_SERIF, STR_NOTO_SANS };
struct I18nStub {
  const char* get(StrId id) const { return id == StrId::STR_NOTO_SERIF ? "Noto Serif" : "Noto Sans"; }
} I18N;
struct SdCardFontFamilyInfo { std::string name; };
class SdCardFontRegistry {
 public:
  std::vector<SdCardFontFamilyInfo> families;
  int getFamilyCount() const { return static_cast<int>(families.size()); }
  const std::vector<SdCardFontFamilyInfo>& getFamilies() const { return families; }
};
struct CrossPointSettings {
  static constexpr int BUILTIN_FONT_COUNT = 2;
  static constexpr int NOTOSERIF = 0;
  static constexpr int NOTOSANS = 1;
};
''' + header + source[begin:end] + r'''
}
void check(const SdCardFontRegistry* registry, const std::vector<std::string>& expected) {
  const auto list = fontdoc::danhSachHo(registry);
  assert(fontdoc::soHo(registry) == static_cast<int>(expected.size()));
  assert(list.size() == expected.size());
  for (int i = 0; i < fontdoc::soHo(registry); ++i) {
    assert(fontdoc::tenHo(registry, i) == expected[i]);
    assert(fontdoc::tenHo(registry, i) == list[i].ten);
    assert(list[i].builtin == (i < 2));
    assert(list[i].chiSo == i);
  }
  for (int invalid : {INT_MIN, -1, fontdoc::soHo(registry), INT_MAX})
    assert(fontdoc::tenHo(registry, invalid).empty());
}
int main() {
  const std::vector<std::string> builtin = {"Noto Serif", "Noto Sans"};
  check(nullptr, builtin);
  SdCardFontRegistry registry;
  check(&registry, builtin);
  // Deliberately unsorted, with a long name: preserve registry order verbatim.
  registry.families = {{"Zulu"}, {"Alpha family with a long SD name"}, {"Middle"}};
  check(&registry, {"Noto Serif", "Noto Sans", "Zulu", "Alpha family with a long SD name", "Middle"});
}
'''
    with tempfile.TemporaryDirectory(prefix="host-", dir=Path(__file__).resolve().parent) as directory:
        directory = Path(directory)
        cpp = directory / "catalog.cpp"
        binary = directory / "catalog"
        cpp.write_text(harness)
        subprocess.run(["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("PASS host catalog: null, 0/3 SD families, ordering, metadata, invalid indices")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("repo", type=Path)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--baseline", action="store_true")
    mode.add_argument("--mutation", action="store_true")
    args = parser.parse_args()
    repo = args.repo.resolve()
    cpp = source_at(repo, CPP, args.baseline)
    header = source_at(repo, HEADER, args.baseline)
    if args.mutation:
        signature = "void EpubReaderActivity::enterFontLevel() {"
        assert cpp.count(signature) == 1
        cpp = cpp.replace(signature, signature + "\n  const auto copiedFamilies = fontdoc::danhSachHo(&sdFontSystem.registry());", 1)
    static_contract(cpp, header)
    print("PASS reader font catalog contract")
    if not args.baseline:
        compiled_catalog_check(repo)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
