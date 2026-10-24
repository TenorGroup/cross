#!/usr/bin/env python3
"""A background chapter build in the reader stops at a key contact (the yieldNow of Section::buildSomeMore).

The build steps of the reader that run between key presses (the quiet-pass build and the layout that catches up
after a text change) hand the predicate over; a step that did not would keep the loop for a whole parse step
after a press. Source check of src/activities/reader/EpubReaderActivity.cpp; the parse-step side is H1's test.
"""
import re
import sys
from pathlib import Path

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
source = (root / "src/activities/reader/EpubReaderActivity.cpp").read_text()
header = (root / "lib/Epub/Epub/Section.h").read_text()
failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


check("bool buttonContactClosed() { return gpio.rawInputActive(); }" in source,
      "buttonContactClosed must read the raw button contact (gpio.rawInputActive)")
check(re.search(r"bool\s+buildSomeMore\(\s*int\s+maxPages\s*,\s*bool\s*\(\*yieldNow\)\(\)\s*=\s*nullptr\s*\)", header),
      "Section::buildSomeMore must take the yieldNow predicate (#4040)")

calls = re.findall(r"buildSomeMore\(([^()]*(?:\([^()]*\))?[^()]*)\)", source)
quiet = [c for c in calls if c.split(",")[0].strip() in ("BACKGROUND_BUILD_PAGES_PER_TICK", "1")]
foreground = [c for c in calls if c.split(",")[0].strip() == "BUILD_PAGES_PER_CHUNK"]
# the quiet-pass build, the next chapter's look-ahead and the catch-up layout
check(len(quiet) == 3, "expected 3 quiet-pass build calls, found %d" % len(quiet))
for call in quiet:
    check("&buttonContactClosed" in call, "a quiet-pass build must pass &buttonContactClosed: buildSomeMore(%s)" % call)
# a build the page waits for runs to the page it needs and never yields
check(len(foreground) >= 1 and all("," not in c for c in foreground), "foreground builds must not take a yield predicate")

# #4040's idle tick is not taken (D18): no second build loop beside the look-ahead.
check("backgroundBuildTick" not in source, "backgroundBuildTick (#4040) must not come back")

if failures:
    print("\n".join("FAIL: " + f for f in failures))
    sys.exit(1)
print("ok")
