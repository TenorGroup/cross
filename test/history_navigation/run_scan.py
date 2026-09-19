#!/usr/bin/env python3
"""Compile unchanged production source and run the history I/O regression."""
import argparse, pathlib, shutil, subprocess
parser=argparse.ArgumentParser()
parser.add_argument('--source-root',type=pathlib.Path)
parser.add_argument('--output',type=pathlib.Path,required=True)
args=parser.parse_args()
repo=pathlib.Path(__file__).resolve().parents[2]
source=args.source_root or repo
out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
for name in ('ReadingStatsStore.cpp','ReadingStatsStore.h'):
    shutil.copy2(source/'src'/name,out/name)
args_compile=['c++','-std=c++20','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
    '-DARDUINOJSON_ENABLE_ARDUINO_STRING=0',
    '-I'+str(repo/'test/history_navigation/stubs'),'-I'+str(out),'-I'+str(repo/'src'),
    '-I'+str(repo/'.pio/libdeps/gh_release/ArduinoJson/src'),
    str(repo/'test/history_navigation/fast_scan.cpp'),
    *[str(repo/'src/util'/s) for s in ('ReadingHabits.cpp','SoLieuDoc.cpp','NgayGio.cpp')],
    '-o',str(out/'HistoryFastScanTest')]
if 'readOpenSnapshot' in (out/'ReadingStatsStore.cpp').read_text():
    args_compile.insert(1,'-DHISTORY_HAS_OPEN_SNAPSHOT=1')
build=subprocess.run(args_compile,capture_output=True,text=True)
(out/'build.log').write_text(build.stdout+build.stderr)
if build.returncode: print(build.stdout+build.stderr);raise SystemExit(build.returncode)
failed=False
for mode in ('measure','contract','faults','oom'):
    result=subprocess.run([str(out/'HistoryFastScanTest'),mode],capture_output=True,text=True)
    (out/(mode+'.log')).write_text(result.stdout+result.stderr)
    print(mode,result.returncode);print(result.stdout+result.stderr)
    failed |= bool(result.returncode)
raise SystemExit(1 if failed else 0)
