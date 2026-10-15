#!/usr/bin/env python3 -I
"""Source-text checks on the automatic sync (firmware-only code, so the text of the one function is read).

1. The wake pull must not drop the CPU to the low-power clock before the book is open.

setPowerSaving(true) with the radio off and no Lock held sets 10 MHz on the X3 (HalPowerManager.cpp).
The pull runs in setup(), where nothing holds a Lock, and the Epub load, the XPath mapping and the book
open that follow would run 12 times slower. The main loop lowers the clock by itself after 3 s without a
key, so the pull leaves it alone; the sleep push (the sleep after it holds its own Lock) may lower it.

2. pullOnWake answers Skipped for a path that is not an .epub before any other work (a text or XTC book
would spend the Wi-Fi wait and then fail to load as an Epub).

3. Turning the switch off forgets what was owed: the debt file goes and the note in memory with it, so
turning it on weeks later pushes nothing from before.
"""
import re
import sys

root = sys.argv[1]
text = open(f"{root}/src/KOReaderAutoSync.cpp", encoding="utf-8").read()


def body(name):
    start = text.find(name)
    if start < 0:
        return ""
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
wake = body("PullResult pullOnWake(")
if not re.match(r"\{\s*if \(!isEpubPath\(path\)\) return PullResult::Skipped;", wake):
    problems.append("pullOnWake must start by skipping a path that is not an epub")
off = body("void switchedOff(")
if "hasPendingNote = false" not in off or "Storage.remove(DEBT_FILE)" not in off:
    problems.append("switchedOff must clear the note in memory and remove the debt file")
settings = open(f"{root}/src/activities/settings/KOReaderSettingsActivity.cpp", encoding="utf-8").read()
branch = settings[settings.index("index == 6"):settings.index("index == 8")]
if not re.search(r"setAutoSync\(!KOREADER_STORE\.getAutoSync\(\)\);\s*if \(!KOREADER_STORE\.getAutoSync\(\)\) koautosync::switchedOff\(\);", branch):
    problems.append("the Automatic sync row must call koautosync::switchedOff() when the switch goes off")
manager = open(f"{root}/src/activities/ActivityManager.cpp", encoding="utf-8").read()
replace = manager[manager.index("void ActivityManager::replaceActivity("):]
guard = re.search(r"void ActivityManager::replaceActivity\([^)]*\) \{(.*?)\n\}", replace, re.S)
if not guard or not re.match(r"\s*if \(!newActivity\)\s*\{.*?\}\s*mappedInput\.resetHomeButtonInput\(\);", guard.group(1), re.S):
    problems.append("replaceActivity must reject a null activity before touching pending state")
before_open = text[text.index("bool beforeOpen("):text.index("bool pullIsArmed(")]
if "markPageTurnerRestartSpent" in before_open:
    problems.append("the radio restart memo must not be spent before the restart activity is allocated")
pull_restart = manager[manager.index("class PullRestartActivity"):manager.index("// Writes a closing screen")]
if not re.search(r"onEnter\(\) override \{.*?markPageTurnerRestartSpent\(\);\s*silentRestartToReader\(\);", pull_restart, re.S):
    problems.append("the radio restart memo must be spent inside the allocated restart activity")
if not re.search(r"if \(!restart\) \{.*?cancelPullRestart\(\);", manager, re.S):
    problems.append("failed restart activity allocation must cancel the armed pull")
cancel = body("void cancelPullRestart(")
if "pullRestartPending = false" not in cancel or "setPullDebt(PullDebt::Owed)" not in cancel:
    problems.append("cancelling a failed restart allocation must clear the pending flag and restore the owed debt")
print("DO: " + "; ".join(problems) if problems else "XANH")
sys.exit(1 if problems else 0)
