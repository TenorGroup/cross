#!/usr/bin/env python3
"""Host contract for file browser back destinations and cursor restoration."""

import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_contract.py REPO_ROOT")
        return 2
    root = Path(sys.argv[1])
    browser = (root / "src/activities/home/FileBrowserActivity.cpp").read_text()
    home = (root / "src/activities/home/HomeActivity.cpp").read_text()
    required = [
        "const HomeMenuItem origin = activityManager.homeMenuOrigin();",
        "origin == HomeMenuItem::FILE_BROWSER || origin == HomeMenuItem::NONE",
        "if (mode == Mode::Books) {",
        "entryForVisibleRow(nav.selected) == dirName",
        "last.find('/', 1) != std::string::npos",
    ]
    missing = [needle for needle in required if needle not in browser + home]
    if missing:
        print("RED: missing navigation contract: " + ", ".join(missing))
        return 1
    if "// restoreDirectory();" in browser:
        print("RED: directory restore is disabled")
        return 1
    if 'lastFolderRow = last != "/";' in home:
        print("RED: direct-child reopen fix removed")
        return 1
    if "lastFolderRow = last != \"/\" && last.find('/', 1) != std::string::npos;" in home:
        print("RED: touch shell lost the baseline reopen row")
        return 1
    print("GREEN: file back destination, row restore, and root reopen contracts present")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
