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
flush_mark = body("void flushMark(")
hash_at = flush_mark.find("documentHash(pendingMarkPath)")
assign_at = flush_mark.find("pendingMark.document = hash")
if hash_at < 0 or assign_at < 0 or hash_at > assign_at:
    problems.append("flushMark must obtain a deferred document hash before storing the mark")
else:
    failed_hash_path = flush_mark[hash_at:assign_at]
    if ("pendingMarkPath.clear()" in failed_hash_path or "hasPendingMark = false" in failed_hash_path or
            "pendingMark = {}" in failed_hash_path):
        problems.append("flushMark must retain the pending path and mark when document hashing fails")
settings = open(f"{root}/src/activities/settings/KOReaderSettingsActivity.cpp", encoding="utf-8").read()
branch = settings[settings.index("index == R::ROW_AUTO_SYNC"):settings.index("index == R::ROW_SYNC_CLIPPINGS")]
if not re.search(r"setAutoSync\(!KOREADER_STORE\.getAutoSync\(\)\);\s*if \(!KOREADER_STORE\.getAutoSync\(\)\) koautosync::switchedOff\(\);", branch):
    problems.append("the Automatic sync row must call koautosync::switchedOff() when the switch goes off")
manager = open(f"{root}/src/activities/ActivityManager.cpp", encoding="utf-8").read()
before_open = text[text.index("bool beforeOpen("):text.index("bool pullIsArmed(")]
if "markPageTurnerRestartSpent" in before_open:
    problems.append("the radio restart memo must not be spent before the restart activity is allocated")
pull_restart = manager[manager.index("class PullRestartActivity"):manager.index("// Writes a closing screen")]
if not re.search(r"onEnter\(\) override \{.*?markPageTurnerRestartSpent\(\);\s*silentRestartToReader\(\);", pull_restart, re.S):
    problems.append("the radio restart memo must be spent inside the allocated restart activity")
if not re.search(r"if \(koautosync::pullRestartReady\(\)\)\s*\{?\s*markPageTurnerRestartSpent\(\);", pull_restart, re.S):
    problems.append("the restart activity must spend the memo only when a saved pull restart is pending")
if not re.search(r"if \(!restart\) \{.*?cancelPullRestart\(\);", manager, re.S):
    problems.append("failed restart activity allocation must cancel the armed pull")
cancel = body("void cancelPullRestart(")
if "pullRestartPending = false" not in cancel or "setPullDebt(PullDebt::Owed)" not in cancel:
    problems.append("cancelling a failed restart allocation must clear the pending flag and restore the owed debt")
pull_gated = body("PullResult pullGated(")
save_at = pull_gated.find("keepPullPath(savedPull, path, PULL_DONE_MAGIC")
decide_at = pull_gated.find("preparePageTurnerAfterPull(")
prefix = pull_gated[:decide_at] if decide_at >= 0 else ""
if decide_at < 0 or "PullResume savedPull" not in prefix:
    problems.append("the pull must preflight the resume path buffer before deciding on a restart")
if save_at < 0 or decide_at < 0 or save_at > decide_at:
    problems.append("the pull must save its resume path after deciding and before publishing the restart")
host = open(f"{root}/src/BlePageTurnerHost.cpp", encoding="utf-8").read()
prepare_start = host.find("bool preparePageTurnerAfterPull(")
prepare_end = host.find("void markPageTurnerRestartSpent(", prepare_start)
prepare = host[prepare_start:prepare_end] if prepare_start >= 0 and prepare_end >= 0 else ""
if "memo.magic = bleturner::kRestartSpentMagic" in prepare:
    problems.append("the restart decision helper must not spend the memo before the resume path is saved")
pull_then_open = before_open[before_open.index("case OpenStep::PullThenOpen:"):before_open.index("case OpenStep::RestartForPull:")]
if "setPullDebt(PullDebt::Armed)" in pull_then_open:
    problems.append("a skipped pull after the owed open must not re-arm debt after planOpen consumed it")
note_start = text[text.index("void noteStart("):text.index("bool beforeOpen(")]
if "documentHash(keptPull.path)" in note_start:
    problems.append("the kept pull must defer document hashing until after the first frame")
if "pendingMarkPath = APP_STATE.openEpubPath" not in note_start:
    problems.append("the kept pull must retain its path for the deferred mark hash")
if "activityManager.deferWrite(&flushMark)" in pull or "activityManager.deferWrite(&flushMark)" in note_start:
    problems.append("the pull mark must not be queued before the reader has drawn its first page")
if "markAfterFramePending = true" not in pull or "markAfterFramePending = hasPendingMark" not in note_start:
    problems.append("an applied or restored pull mark must arm one first-frame write attempt")
if "markAfterFramePending = false" not in off:
    problems.append("turning automatic sync off must clear the first-frame mark attempt")
epub_reader = open(f"{root}/src/activities/reader/EpubReaderActivity.cpp", encoding="utf-8").read()
if "if (!openCommitPending) koautosync::afterFirstFrame();" not in epub_reader:
    problems.append("the EPUB loop must flush a pending pull mark only after the first rendered page")
flush_hint_at = push.find("flushHint()")
empty_debt_at = push.find("if (!loadDebts(list) || list.empty()) return;")
if flush_hint_at < 0 or empty_debt_at < 0 or flush_hint_at > empty_debt_at:
    problems.append("pushBeforeSleep must flush a pending Wi-Fi hint before the empty debt return")
fast_success_at = text.find("if (fastConnected) {")
fast_success = text[fast_success_at:text.find("return true;", fast_success_at)] if fast_success_at >= 0 else ""
if "pendingHint = hint" not in fast_success or "hasPendingHint = true" not in fast_success:
    problems.append("a successful fast join must replace pending hint state with a zero-failure hint")
resume = open(f"{root}/lib/KOReaderSync/KOReaderPullResume.h", encoding="utf-8").read()
if "constexpr uint32_t PULL_JOIN_MS" not in resume:
    problems.append("the pull join must have a named budget")
pull_now = body("PullResult pullNow(")
if "joinSavedNetwork(networkAt + PULL_JOIN_MS)" not in pull_now:
    problems.append("pullNow must use the named pull join budget")
autosync = text
if "hintFailureMemo = {HINT_FAILURE_MAGIC, hintKey(stored), 2, 0};" not in autosync:
    problems.append("a changed access point must disable the old hint across a pre-frame restart")
if "hintFailureMemo = {HINT_FAILURE_MAGIC, hintKey(now), 0, 1};" not in autosync:
    problems.append("a successful scan must arm a reset marker for the current hint")
client = open(f"{root}/lib/KOReaderSync/KOReaderSyncClient.cpp", encoding="utf-8").read()
deadline_json = (r"hasDeadline\s*\?\s*deserializeJson\(doc,\s*reinterpret_cast<const char\*>\(deadlineBody\.bytes\(\)\),"
                 r"\s*deadlineBody\.size\(\)\)")
if not re.search(deadline_json, client):
    problems.append("the deadline JSON body must be parsed from its existing byte buffer")
print("DO: " + "; ".join(problems) if problems else "XANH")
sys.exit(1 if problems else 0)
