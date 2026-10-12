#!/usr/bin/env python3
import argparse
import re
from pathlib import Path


parser = argparse.ArgumentParser()
parser.add_argument("--header", type=Path, required=True)
args = parser.parse_args()
header = args.header.read_text()
match = re.search(r"kSearchRows\s*=\s*([^;]+);", header)
if match is None:
    raise AssertionError("File Browser must expose one search-row policy")
policy = match.group(1).strip()
if "kTouchShell ? 1 : 0" in policy:
    raise AssertionError("X4 Pro Search row is visible by default: " + policy)
repo = args.header.resolve().parents[3]
base = (repo / "src/activities/UiListActivity.h").read_text()
home = (repo / "src/activities/home/HomeActivity.h").read_text()
home_source = (repo / "src/activities/home/HomeActivity.cpp").read_text()
list_source = (repo / "src/activities/UiListActivity.cpp").read_text()
assert 'virtual bool canSearchFiles() const { return false; }' in base
assert 'tenorchrome::kTouchShell && activeTabId == Tab::FOLDER' in home
overrides = [path.name for path in (repo / "src/activities").rglob("*.h")
             if 'canSearchFiles() const override' in path.read_text()]
assert sorted(overrides) == ["FileBrowserActivity.h", "HomeActivity.h"], overrides
assert 'filesearch::buildResults(mucTheNho, folderSearchQuery, folderRowIndices' in home_source
assert 'library::fold' not in home_source
assert 'buildFileSearch(screen);' in home_source
assert 'if (!tenorchrome::kTouchShell || !canSearchFiles()) return;' in list_source
assert 'filesearch::SEARCH_HEIGHT, filesearch::SEARCH_GAP' in list_source
assert 'fileSearchDragAtTop = activeNav().top == 0;' in list_source
assert 'fileSearchDragBlocked |= contacts != 1 || rowMenu.isActive();' in list_source
assert 'drag.startX < renderer.getScreenWidth() - 16' in list_source
assert 'routeFileSearchDrag()) return;' in list_source
drag_route = list_source.split('bool UiListActivity::routeFileSearchDrag() {', 1)[1].split('\n}', 1)[0]
assert 'RenderLock' not in drag_route
assert 'pendingFileSearchReveal = nextReveal;' in drag_route
assert 'fileSearchRevealPending = true;' in drag_route
assert 'fileSearchReveal = pendingFileSearchReveal;' in list_source
print("File Browser search row is conditional: " + policy)
