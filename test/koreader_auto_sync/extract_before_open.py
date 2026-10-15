#!/usr/bin/env python3
"""Extract one production definition for the host behavior tests."""

import pathlib
import sys


source_path = pathlib.Path(sys.argv[1])
output_path = pathlib.Path(sys.argv[2])
source = source_path.read_text(encoding="utf-8")
target = sys.argv[3] if len(sys.argv) > 3 else "beforeOpen"
targets = {
    "beforeOpen": ("bool beforeOpen(", ("planOpen(", "pullGated(", "keepPullPath(")),
    "pullGated": ("PullResult pullGated(", ("pullNow(", "keepPullPath(", "preparePageTurnerAfterPull(")),
    "noteStart": ("void noteStart(", ("takePullMagic(", "APP_STATE.openEpubPath", "debtAfterStart(")),
    "PullRestartActivity": ("class PullRestartActivity", ("pullRestartReady(", "markPageTurnerRestartSpent(")),
    "afterFirstFrame": ("void afterFirstFrame(", ("hasPendingMark", "RenderLock::TryTake", "flushMark()")),
}
signature, required = targets[target]
start = source.index(signature)
opening = source.index("{", start)
depth = 0
end = opening
while end < len(source):
    depth += source[end] == "{"
    depth -= source[end] == "}"
    end += 1
    if depth == 0:
        break
else:
    raise RuntimeError("unterminated beforeOpen definition")

if target == "PullRestartActivity" and source[end:end + 1] == ";":
    end += 1
definition = source[start:end]
if any(token not in definition for token in required):
    raise RuntimeError(f"{target} definition lost a required production decision")
output_path.write_text(definition + "\n", encoding="utf-8")
