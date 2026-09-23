#!/usr/bin/env python3
"""Compile the production reader turn functions with host clock, lock and log stubs."""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess


parser = argparse.ArgumentParser()
parser.add_argument("source", type=pathlib.Path)
parser.add_argument("output", type=pathlib.Path)
parser.add_argument("--compiler", default="c++")
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
reader_cpp = args.source / "src/activities/reader/ReaderActivity.cpp"
reader_h = args.source / "src/activities/reader/ReaderActivity.h"
epub_cpp = args.source / "src/activities/reader/EpubReaderActivity.cpp"
epub_h = args.source / "src/activities/reader/EpubReaderActivity.h"
cpp = reader_cpp.read_text()
header = reader_h.read_text()
epub = epub_cpp.read_text()
epub_header = epub_h.read_text()


def function(name, text=None, owner="ReaderActivity"):
    text = cpp if text is None else text
    match = re.search(r"\b" + owner + "::" + name + r"\s*\(", text)
    if not match:
        raise ValueError("Missing production function: " + owner + "::" + name)
    start = text.rfind("\n", 0, match.start()) + 1
    brace = text.index("{", match.end())
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


def block(text, start):
    brace = text.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


trace_match = re.search(r"(#ifdef TENOR_(?:UI_ACCEPTANCE|TURN_TRACE)\n  struct TurnTrace \{.*?\n#endif)", header, re.S)
trace_fields = trace_match.group(1) if trace_match else ""
trace_present = bool(trace_match)
epub_trace_match = re.search(r"(?m)^  TurnTrace pendingManualTurnTrace;", epub_header)
epub_fields = re.search(r"(?m)^  int8_t pendingManualTurn = 0;", epub_header).group(0)
if epub_trace_match:
    epub_fields += "\n#ifdef TENOR_TURN_TRACE\n" + epub_trace_match.group(0) + "\n#endif"
names = ["pageTurn", "pageTurnLocked", "luotLatTrangNgoai", "processExternalPageTurn", "onPause", "onResume"]
if "ReaderActivity::queuePageTurn(" in cpp:
    names.append("queuePageTurn")
if trace_present:
    names = ["detectTurnTrace", "logTurnTrace", "replaceQueuedTurnTrace", "dropTurnTrace"] + names

fixture = pathlib.Path(__file__).with_name("fixture.hpp").read_text()
fixture = fixture.replace("@@TRACE_PRESENT@@", "1" if trace_present else "0")
fixture = fixture.replace("@@TRACE_FIELDS@@", trace_fields)
fixture = fixture.replace("@@EPUB_FIELDS@@", epub_fields)
epub_pause = "void EpubReaderActivity::onPause()" in epub
fixture = fixture.replace("@@EPUB_ON_PAUSE@@", "  void onPause() override;" if epub_pause else "")

guard = re.search(r"(?m)^  const bool turnGuardActive = RenderLock::peek\(\) \|\| !manualPageTurnReady\(\);", epub).group(0)
drain_start = epub.index("  if (pendingManualTurn != 0 && !turnGuardActive) {")
drain = "void EpubReaderActivity::drainManual() {\n" + guard + "\n" + block(epub, drain_start) + "\n}"
manual_end = epub.index("  if (pageTurn(!prevPageTriggered)) requestUpdate();", drain_start)
manual_start = epub.rfind("  if (!section) {", drain_start, manual_end)
manual_slice = epub[manual_start:manual_end + len("  if (pageTurn(!prevPageTriggered)) requestUpdate();")]
manual = ("void EpubReaderActivity::manualInput(bool prevTriggered, bool prevPageTriggered, "
          "bool touchTriggered, bool fromTilt) {\n"
          "  struct Touch { bool prev, next; } touch{touchTriggered, false};\n"
          "  struct Turns { bool fromTilt; } turns{fromTilt};\n"
          "  (void)touch; (void)turns;\n" + guard + "\n" + manual_slice + "\n}")
# One loop pass that drains the guarded queue and then reads a press from the
# same pass, in production order: a return inside the drain would skip the press.
tick = ("void EpubReaderActivity::drainThenInput(bool prevTriggered, bool prevPageTriggered, "
        "bool touchTriggered, bool fromTilt) {\n"
        "  struct Touch { bool prev, next; } touch{touchTriggered, false};\n"
        "  struct Turns { bool fromTilt; } turns{fromTilt};\n"
        "  (void)touch; (void)turns;\n" + guard + "\n" + block(epub, drain_start) + "\n" +
        manual_slice + "\n}")
menu_start = epub.index("void EpubReaderActivity::openReaderMenu() {")
menu_body = epub.index("\n", menu_start) + 1
menu_end = epub.index("  if (usesToolbarMenu())", menu_body)
menu = "void EpubReaderActivity::cancelManualForReaderMenu() {\n" + epub[menu_body:menu_end] + "}"
exit_body = function("onExit", epub, "EpubReaderActivity")
exit_prefix = exit_body[exit_body.index("{") + 1:exit_body.index("  if (!preview")]
exit_method = "void EpubReaderActivity::onExit() {" + exit_prefix + "  ReaderActivity::onExit();\n}"

cases = pathlib.Path(__file__).with_name("cases.cpp").read_text()
projection = args.output / "projection.cpp"
projection.write_text(fixture + "\n" + "\n\n".join(function(name) for name in names) +
                      "\n" + "\n\n".join((drain, manual, tick, menu)) + "\n" +
                      (function("onPause", epub, "EpubReaderActivity") if epub_pause else "") +
                      "\n" + exit_method + "\n" + cases)
(args.output / "source-hashes.json").write_text(json.dumps({
    str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in (reader_cpp, reader_h, epub_cpp, epub_h)
}, indent=2))
command = [args.compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
           "-fsanitize=address,undefined", "-g", "-DTENOR_UI_ACCEPTANCE", "-DTENOR_TURN_TRACE", str(projection),
           "-o", str(args.output / "projection")]
(args.output / "compile-command.json").write_text(json.dumps(command, indent=2))
compiled = subprocess.run(command, capture_output=True, text=True)
(args.output / "compile.log").write_text(compiled.stdout + compiled.stderr)
if compiled.returncode:
    print(compiled.stderr)
    raise SystemExit(compiled.returncode)
run = subprocess.run([str(args.output / "projection")], capture_output=True, text=True)
(args.output / "results.log").write_text(run.stdout + run.stderr)
print(run.stdout + run.stderr, end="")
raise SystemExit(run.returncode)
