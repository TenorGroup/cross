#!/usr/bin/env python3
"""Compare what KOReaderSyncClient sends with what v1.0.76 sent, for every configuration a v1.0.76 device holds.

  run.py                      build this tree, compare with golden-v1.0.76.txt
  run.py --make-golden REF    build REF (a git tag) instead and write golden-v1.0.76.txt from it
Environment: ARDUINOJSON_DIR (the ArduinoJson src folder), SDK_DIR (the freeink-sdk checkout, default
<repo>/freeink-sdk), CXX.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
GOLDEN = HERE / "golden-v1.0.76.txt"


def build_and_run(tree: Path, work: Path) -> str:
    sdk = Path(os.environ.get("SDK_DIR", REPO / "freeink-sdk"))
    link = tree / "freeink-sdk"
    if not link.exists():
        link.symlink_to(sdk)
    json_dir = os.environ.get("ARDUINOJSON_DIR", str(REPO / ".pio/libdeps/gh_release/ArduinoJson/src"))
    exe = work / "wire"
    cmd = [os.environ.get("CXX", "c++"), "-std=c++20", "-Wno-character-conversion", "-DARDUINOJSON_ENABLE_ARDUINO_STRING=0",
           "-I" + str(HERE / "stubs"), "-I" + str(HERE.parent / "koreader_progress/stubs"),
           "-I" + str(HERE.parent / "host_stubs"), "-I" + str(tree / "lib/KOReaderSync"), "-I" + str(REPO / "lib/hal"),
           "-I" + str(REPO / "lib/HttpHeader"), "-I" + str(tree / "lib/NetworkTrust"), "-I" + json_dir,
           str(HERE / "wire_main.cpp"), str(tree / "lib/KOReaderSync/KOReaderCredentialStore.cpp"),
           str(tree / "lib/KOReaderSync/KOReaderSyncClient.cpp"), "-o", str(exe)]
    subprocess.run(cmd, check=True)
    return subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--make-golden")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="koreader-wire-") as tmp:
        work = Path(tmp)
        if args.make_golden:
            tree = work / "tree"
            tree.mkdir()
            archive = subprocess.run(["git", "-C", str(REPO), "archive", args.make_golden, "lib/KOReaderSync", "lib/NetworkTrust",
                                      "src/platform"], check=True, capture_output=True).stdout
            subprocess.run(["tar", "-x", "-C", str(tree)], input=archive, check=True)
            GOLDEN.write_text(build_and_run(tree, work))
            print("wrote", GOLDEN)
            return 0
        # This tree as it is: copy only what the build reads, so the sdk link goes beside it.
        tree = work / "tree"
        for sub in ("lib/KOReaderSync", "lib/NetworkTrust", "src/platform"):
            shutil.copytree(REPO / sub, tree / sub)
        out = build_and_run(tree, work)
    if out != GOLDEN.read_text():
        sys.stdout.write("KOSync requests differ from v1.0.76:\n")
        import difflib
        sys.stdout.writelines(difflib.unified_diff(GOLDEN.read_text().splitlines(True), out.splitlines(True),
                                                   "v1.0.76", "this tree"))
        return 1
    print("KOSync requests match v1.0.76 in", out.count("=== "), "configurations")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
