#!/usr/bin/env python3
"""Check native String JSON reads using the simulator's configured build flags."""
import argparse
import configparser
from pathlib import Path
import shlex
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--simulator-root", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
here = Path(__file__).resolve().parent
repo = here.parents[1]
config = configparser.ConfigParser(interpolation=None)
config.read(repo / "platformio.ini")
flags = [flag for flag in shlex.split(config["env:simulator"]["build_flags"])
         if flag.startswith("-DARDUINOJSON_")]
args.output.mkdir(parents=True, exist_ok=True)
json_root = args.simulator_root.parent / "ArduinoJson" / "src"
failed = False
for order in ("json_first", "arduino_first"):
    includes = ["ArduinoJson.h", "Arduino.h"]
    if order == "arduino_first":
        includes.reverse()
    source = args.output / (order + ".cpp")
    source.write_text("".join(f"#include <{name}>\n" for name in includes)
                      + '#include "roundtrip.h"\n')
    binary = args.output / order
    subprocess.run(["c++", "-std=c++17", *flags,
                    "-I" + str(args.simulator_root / "src"),
                    "-I" + str(json_root), "-I" + str(here),
                    str(source), "-o", str(binary)], check=True)
    run = subprocess.run([str(binary)], capture_output=True, text=True)
    output = run.stdout + run.stderr
    (args.output / (order + ".log")).write_text(output)
    print(order + ":\n" + output, end="")
    failed |= run.returncode != 0
raise SystemExit(int(failed))
