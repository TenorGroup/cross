#!/usr/bin/env python3 -I
"""The wake pull must not drop the CPU to the low-power clock before the book is open.

setPowerSaving(true) with the radio off and no Lock held sets 10 MHz on the X3 (HalPowerManager.cpp).
The pull runs in setup(), where nothing holds a Lock, and the Epub load, the XPath mapping and the book
open that follow would run 12 times slower. The main loop lowers the clock by itself after 3 s without a
key, so the pull leaves it alone; the sleep push (the sleep after it holds its own Lock) may lower it.
Firmware-only code, so this checks the source text of the one function that matters.
"""
import re
import sys

root = sys.argv[1]
text = open(f"{root}/src/KOReaderAutoSync.cpp", encoding="utf-8").read()


def body(name):
    start = text.index(name)
    depth, i = 0, text.index("{", start)
    first = i
    while True:
        depth += text[i] == "{"
        depth -= text[i] == "}"
        i += 1
        if depth == 0:
            return text[first:i]


leave = body("void leaveWifi(")
pull = body("PullResult pullNow(")
push = body("void pushBeforeSleep(")
problems = []
if not re.search(r"if \(restCpu\)\s*powerManager\.setPowerSaving\(true\)", leave):
    problems.append("leaveWifi lowers the clock whatever the caller")
calls = re.findall(r"leaveWifi\(([^)]*)\)", pull)
if not calls or any(c.strip() != "false" for c in calls):
    problems.append(f"pullNow must call leaveWifi(false) every time, found {calls}")
if re.findall(r"leaveWifi\(([^)]*)\)", push) != ["true"]:
    problems.append("pushBeforeSleep must call leaveWifi(true) once")
print("DO: " + "; ".join(problems) if problems else "XANH")
sys.exit(1 if problems else 0)
