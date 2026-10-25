#!/usr/bin/env python3
"""Every outcome a quick action resolves to is carried out by main.cpp, and a clip reaches the reader.

QuickAction.h can resolve a gesture to any Outcome; runQuickAction() in src/main.cpp is the one switch that does it.
An outcome without its case does nothing (the double tap and shake bound to "Create clipping" did exactly that), so
each enumerator but None needs a case there, and the clip case must hand the reader the Clip shortcut the reader
takes up.
"""
import re
import sys
from pathlib import Path

repo = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
header = (repo / "src/QuickAction.h").read_text()
main = (repo / "src/main.cpp").read_text()
reader = (repo / "src/activities/reader/EpubReaderActivity.cpp").read_text()

enum = header[header.index("enum class Outcome"):]
enum = enum[:enum.index("};")]
outcomes = [o for o in re.findall(r"\b([A-Z]\w+)\b", enum.split("{", 1)[1]) if o != "None"]

start = main.index("static void runQuickAction(const uint8_t action")
body = main[start:main.index("\n}\n", start)]
failures = []
for outcome in outcomes:
    if f"case quickaction::Outcome::{outcome}:" not in body:
        failures.append(f"RED runQuickAction has no case for Outcome::{outcome}")
clip = body[body.find("case quickaction::Outcome::Clip:"):] if "case quickaction::Outcome::Clip:" in body else ""
if "ReaderShortcut::Clip" not in clip.split("break;")[0]:
    failures.append("RED Outcome::Clip does not ask the reader for ReaderShortcut::Clip")
if "shortcut == ReaderShortcut::Clip" not in reader:
    failures.append("RED the reader does not take up ReaderShortcut::Clip")
print("\n".join(failures) if failures else "GREEN: every quick action outcome has its case, and the clip reaches the reader")
sys.exit(1 if failures else 0)
