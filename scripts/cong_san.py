#!/usr/bin/env python3
"""X3 heap floor gate (release rule 20): reads serial logs of the heap gate build and fails on any
heap state under the floor, free < 10,240 B or largest block < 8,192 B (the API reports a block of
8,192 B as 8,180 with light poisoning, so reported numbers are compared with 8,180).

The floor is measured before the reader asks for its 7,920 B grayscale strip and after every other
allocation. While the strip is held (PAINT_GRAY_DONE, PAINT_CLEANUP_END, other tasks' allocations
logged with strip=1) the floor is not compared; there the gate fails on an allocation that failed,
and it lists every allocation larger than the largest block left after it.

An allocation that the heap floor door took and gave straight back (an ALLOC mark matched by a
"[HEAP_FLOOR] refuse" line of the same size just before it) is listed as door_test and does not fail.

Fails also on: abort, STORE_FAILED, Build starved, HID begin skipped, rolled back, Radio still
stopping, OOM on the strip. Exit 1 when the gate fails. With no argument it runs its self-check.

  python3 scripts/cong_san.py <serial.log> [...]
"""
import re
import sys

FREE_FLOOR = 10240
LARGEST_FLOOR = 8192 - 12
HELD = ("GRAY_DONE", "CLEANUP_END")  # paint marks taken while the strip is held
STRIP_BYTES = 7920  # the strip itself (99 x 80): the heap after it is inside the window
PAINT = re.compile(r"\[ERS_TRACE\] PAINT_(\w+) rid=(\d+).*?kind=(\S+) heap=(\d+) largest=(\d+)")
ALLOC = re.compile(r"\[HEAP_FLOOR\] cross where=ALLOC (.*)")
BIG = re.compile(r"\[BIG\] t=\d+ size=(\d+) caps=\S+ task=(\S+) ok=(\d) free=(\d+) largest=(\d+) strip=1")
REFUSE = re.compile(r"\[HEAP_FLOOR\] refuse (.*)")
BLOCK = re.compile(r"abort\(\)|Guru Meditation|STORE_FAILED|Build starved|HID begin skipped|rolled back|"
                   r"Radio still stopping|OOM: grayscale strip scratch")


def under(free, largest):
    return free < FREE_FLOOR or largest < LARGEST_FLOOR


def scan(lines):
    """Returns a dict of lists of (line number, text): outside (floor crossed outside the strip window),
    window_fail (an allocation failed while the strip was held), window_big (an allocation larger than
    the largest block left after it), window (other tasks' allocations in the window), held (paint
    marks taken while the strip was held, not compared), blockers, refusals."""
    r = {k: [] for k in ("outside", "door_test", "window_fail", "window_big", "window", "held", "blockers", "refusals")}
    pending = []  # (line, bytes) of door refusals not yet matched with their ALLOC mark
    for n, line in enumerate(lines, 1):
        if BLOCK.search(line):
            r["blockers"].append((n, line.strip()[-140:]))
        m = PAINT.search(line)
        if m:
            free, largest = int(m.group(4)), int(m.group(5))
            text = f"PAINT_{m.group(1)} rid={m.group(2)} kind={m.group(3)} free={free} largest={largest}"
            if m.group(1) in HELD:
                r["held"].append((n, text))
            elif under(free, largest):
                r["outside"].append((n, text))
            continue
        m = ALLOC.search(line)
        if m:
            size = re.search(r"size=(\d+)", m.group(1))
            hit = next((p for p in pending if size and p[1] == int(size.group(1)) and n - p[0] <= 60), None)
            if size and int(size.group(1)) == STRIP_BYTES:
                r["held"].append((n, "ALLOC " + m.group(1).strip()))
            elif hit:
                pending.remove(hit)
                r["door_test"].append((n, "ALLOC " + m.group(1).strip()))
            else:
                r["outside"].append((n, "ALLOC " + m.group(1).strip()))
            continue
        m = BIG.search(line)
        if m:
            size, ok, largest = int(m.group(1)), m.group(3) == "1", int(m.group(5))
            text = f"size={size} task={m.group(2)} ok={int(ok)} free={m.group(4)} largest={largest}"
            r["window"].append((n, text))
            if not ok:
                r["window_fail"].append((n, text))
            elif m.group(2) != "isr" and size > largest:
                r["window_big"].append((n, text))
            continue
        m = REFUSE.search(line)
        if m:
            r["refusals"].append((n, m.group(1).strip()))
            b = re.search(r"bytes=(\d+)", m.group(1))
            if b:
                pending.append((n, int(b.group(1))))
    return r


def demo():
    sample = [
        "[1] [INF] [ERS_TRACE] PAINT_GRAY_DONE rid=31 id=26 t=1 elapsed_ms=1 input_ms=1 spine=18 page=0 kind=strip heap=10964 largest=3700 min=9940",
        "[2] [INF] [ERS_TRACE] PAINT_COMPLETE rid=31 id=26 t=2 elapsed_ms=1 input_ms=1 spine=18 page=0 kind=page heap=34340 largest=22516 min=9940",
        "[3] [INF] [ERS_TRACE] PAINT_LOAD_END rid=1 id=0 t=3 elapsed_ms=1 input_ms=0 spine=1 page=0 kind=ok heap=10240 largest=8180 min=1",
        "[4] [INF] [ERS_TRACE] PAINT_BEGIN rid=2 id=0 t=4 elapsed_ms=1 input_ms=0 spine=1 page=0 kind=page heap=10239 largest=9000 min=1",
        "[ERR] [HEAP_FLOOR] cross where=PAINT_BEGIN kind=page t=4 free=10239 largest=9000 floor_free=10240 floor_largest=8180",
        "[HEAP_FLOOR] cross where=ALLOC size=8192 task=loopTask t=5 free=9000 largest=9000 floor_free=10240 floor_largest=8180",
        "[BIG] t=7 size=3072 caps=0x1804 task=nimble_host ok=1 free=9800 largest=2000 strip=1",
        "[BIG] t=8 size=1024 caps=0x1804 task=nimble_host ok=1 free=20000 largest=12000 strip=1",
        "[BIG] t=9 size=64 caps=0x1804 task=btController ok=0 free=8000 largest=40 strip=1",
        "[INF] [HEAP_FLOOR] refuse who=inflate bytes=32768 free=40000 largest=20000",
        "[ERR] [ERS] Build starved of heap free=16320 largest=11764",
        "[HEAP_FLOOR] cross where=ALLOC size=32768 task=loopTask t=9 free=23400 largest=7156 floor_free=10240 floor_largest=8180",
        "[HEAP_FLOOR] cross where=ALLOC size=7920 task=ActivityMan t=10 free=8896 largest=4596 floor_free=10240 floor_largest=8180",
    ]
    r = scan(sample)
    assert [n for n, _ in r["outside"]] == [4, 6], r["outside"]  # the paint mark once, the allocation once
    assert [n for n, _ in r["held"]] == [1, 13] and [n for n, _ in r["window"]] == [7, 8, 9]
    assert [n for n, _ in r["window_fail"]] == [9] and [n for n, _ in r["window_big"]] == [7]
    assert [n for n, _ in r["blockers"]] == [11] and len(r["refusals"]) == 1
    assert [n for n, _ in r["door_test"]] == [12]
    print("cong_san self-check: ok")


def main(paths):
    fail = 0
    for path in paths:
        r = scan(open(path, errors="replace").read().splitlines())
        print(f"{path}: " + ", ".join(f"{k} {len(v)}" for k, v in r.items()))
        for key in ("outside", "window_fail", "blockers", "window_big", "refusals"):
            for n, text in r[key][:30]:
                print(f"  {key} line {n}: {text}")
        fail += len(r["outside"]) + len(r["window_fail"]) + len(r["blockers"])
    print("FAIL" if fail else "PASS", f"outside+window_fail+blockers={fail}")
    return 1 if fail else 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        demo()
        sys.exit(0)
    sys.exit(main(sys.argv[1:]))
