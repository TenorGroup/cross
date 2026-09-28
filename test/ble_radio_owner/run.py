#!/usr/bin/env python3
"""Only lib/BlePageTurner may touch the SDK radio: no other app file includes BleKeyboardHost.h,
names the BleKeyboardHost class or its BleHid/BleKbd shorthands, so no other file can begin or
end the radio behind the module's back."""
import argparse
import pathlib
import re
import sys

parser = argparse.ArgumentParser()
parser.add_argument("--source", required=True, type=pathlib.Path)
args = parser.parse_args()
root = args.source.resolve()
owner = root / "lib" / "BlePageTurner"
pattern = re.compile(r"BleKeyboardHost|\bBleHid\b|\bBleKbd\b")
found = []
scanned = 0
for base in (root / "src", root / "lib"):
    for path in sorted(base.rglob("*")):
        if path.suffix not in (".h", ".hpp", ".c", ".cpp") or owner in path.parents:
            continue
        scanned += 1
        for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
            if pattern.search(line):
                found.append(f"{path.relative_to(root)}:{number}: {line.strip()}")
print(f"scanned {scanned} files outside lib/BlePageTurner")
if scanned < 100:
    print("FAIL: the scan did not find the app sources")
    sys.exit(1)
if found:
    print("FAIL: the radio is touched outside lib/BlePageTurner:")
    print("\n".join(found))
    sys.exit(1)
print("PASS: only lib/BlePageTurner touches BleKeyboardHost")
