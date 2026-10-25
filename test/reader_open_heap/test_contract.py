#!/usr/bin/env python3
"""The reader activity lives on the heap for as long as a book is open, so what it holds by value is paid
on every page of every book (X3: ESP32-C3, about 45 KB free while the radio runs).

v1.1.0 carried a second OptionPopup by value (the protected-book dialog, 936 B of the activity's 3,216 B on
the host build, 2,120 B on v1.0.76) although almost no book ever needs it. It is allocated when it is shown.

Run with the repo path; --baseline checks git HEAD, --mutation puts the by-value member back and must fail.
"""
import argparse
import re
import subprocess
from pathlib import Path

HEADER = "src/activities/reader/EpubReaderActivity.h"
CPP = "src/activities/reader/EpubReaderActivity.cpp"
# v1.0.76 held exactly one popup by value, the in-reader menu overlay.
POPUPS_BY_VALUE = 1


def source_at(repo: Path, path: str, baseline: bool) -> str:
    if not baseline:
        return (repo / path).read_text()
    return subprocess.check_output(["git", "show", "HEAD:" + path], cwd=repo, text=True)


def check(header: str, cpp: str) -> None:
    by_value = re.findall(r"^\s*OptionPopup\s+(\w+);", header, flags=re.M)
    assert len(by_value) <= POPUPS_BY_VALUE, f"OptionPopup held by value: {by_value} (v1.0.76 held one)"
    assert re.search(r"std::unique_ptr<OptionPopup>\s+loadFailurePopup;", header), "loadFailurePopup is not lazy"
    assert "makeUniqueNoThrow<OptionPopup>" in cpp, "the dialog is not allocated where it is shown"
    assert not re.search(r"\bloadFailurePopup\.", cpp), "loadFailurePopup is used as an object"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("repo", type=Path)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--baseline", action="store_true")
    mode.add_argument("--mutation", action="store_true")
    args = parser.parse_args()
    repo = args.repo.resolve()
    header = source_at(repo, HEADER, args.baseline)
    cpp = source_at(repo, CPP, args.baseline)
    if args.mutation:
        header = header.replace("std::unique_ptr<OptionPopup> loadFailurePopup;", "OptionPopup loadFailurePopup;", 1)
    check(header, cpp)
    print("PASS reader open heap contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
