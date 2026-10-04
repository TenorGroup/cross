#!/usr/bin/env python3
"""Turns the STROKE/SCRIBBLE lines of a probe log (CMD:STROKE_LOG 1 <label>) into a samples file
for ScribbleTest, in the format of samples.txt. The strokes between two SCRIBBLE lines are one
gesture; its expected result is the label the person drew under. A stroke line whose point count
is not its n= (cut short on the USB link) drops its gesture, with a note on stderr.

    log_to_samples.py <probe.log> [samples-<name>.txt]
    log_to_samples.py --selftest
"""
import re
import sys
from pathlib import Path

KINDS = {"tap", "swipe", "cross", "circle", "unknown"}
STROKE = re.compile(r"STROKE lbl=(\S+) n=(\d+) gap=(\d+) pts=(\S*)")
SCRIBBLE = re.compile(r"SCRIBBLE lbl=(\S+) kind=(\w+)")


def convert(lines):
    rows, notes, pending, broken = [], [], [], False
    count = {}
    for line in lines:
        m = STROKE.search(line)
        if m:
            label, n, gap, pts = m.group(1), int(m.group(2)), int(m.group(3)), m.group(4).rstrip(";")
            got = len(pts.split(";")) if pts else 0
            if got != n or not all(re.fullmatch(r"-?\d+,-?\d+,\d+", p) for p in pts.split(";")):
                broken = True
                notes.append(f"stroke cut short ({got}/{n} points): {line.strip()[:80]}")
            pending.append((label, gap, pts))
            continue
        m = SCRIBBLE.search(line)
        if not m or not pending:
            continue
        label, said = m.group(1), m.group(2)
        if broken:
            notes.append(f"dropped a {label} gesture with a cut stroke")
        elif label not in KINDS:
            notes.append(f"label '{label}' is none of {sorted(KINDS)}; gesture skipped")
        else:
            count[label] = count.get(label, 0) + 1
            gap = pending[1][1] if len(pending) > 1 else 0
            rows.append(f"real-{label}-{count[label]:02d} {label} {gap} " + " ".join(p for _, _, p in pending) +
                        f"  # device: {said}")
        pending, broken = [], False
    return rows, notes


def selftest():
    log = [
        "[1200] STROKE lbl=cross n=3 gap=5000 pts=10,10,0;60,60,10;110,110,10;",
        "noise in between",
        "[1500] STROKE lbl=cross n=3 gap=250 pts=110,10,0;60,60,10;10,110,10;",
        "[1500] SCRIBBLE lbl=cross kind=cross strokes=2 at=60,60 box=10,10,110,110",
        "[3000] STROKE lbl=cross n=3 gap=1500 pts=10,10,0;60,60,10;",  # cut short
        "[3700] SCRIBBLE lbl=cross kind=swipe strokes=1 at=10,10 box=10,10,60,60",
        "[4000] STROKE lbl=tap n=1 gap=300 pts=5,5,0;",
        "[4000] SCRIBBLE lbl=tap kind=tap strokes=1 at=5,5 box=5,5,5,5",
    ]
    rows, notes = convert(log)
    assert rows == [
        "real-cross-01 cross 250 10,10,0;60,60,10;110,110,10 110,10,0;60,60,10;10,110,10  # device: cross",
        "real-tap-01 tap 0 5,5,0  # device: tap",
    ], rows
    assert len(notes) == 2, notes
    print("log_to_samples selftest ok")


def main():
    if sys.argv[1:] == ["--selftest"]:
        return selftest()
    src = Path(sys.argv[1])
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).with_name(f"samples-{src.stem}.txt")
    rows, notes = convert(src.read_text(errors="replace").splitlines())
    for n in notes:
        print(n, file=sys.stderr)
    out.write_text(f"# From {src.name} by log_to_samples.py.\n" + "\n".join(rows) + "\n")
    print(f"{len(rows)} gestures -> {out}")


if __name__ == "__main__":
    main()
