"""Measure actual reader body pixels for the four public ink levels."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

from PIL import Image

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "test/reading_stats_simulator"))
from cai_dat_truoc_tenor import truoc_tenor
from test_reader_ink_v108 import write_epub, bounds


def check(program, output, pack, board, warm):
    output.mkdir(parents=True, exist_ok=True)
    measurements = []
    for level in range(4):
        case = output / ("level-%d" % level)
        case.mkdir()
        store = case / ".crosspoint"
        store.mkdir()
        family = "InkFixture"
        if pack:
            family = "Literata"
            shutil.copytree(pack / ".fonts" / family, case / ".fonts" / family)
        else:
            font = case / ".fonts" / family
            font.mkdir(parents=True)
            shutil.copyfile(REPO / "lib/EpdFont/builtinFonts/source/BeVietnamPro/BeVietnamPro-Regular.ttf",
                            font / "Regular.ttf")
        write_epub(case / "ink.epub")
        settings = truoc_tenor(dict(language="VI", fontSize=16, sdFontFamilyName=family,
                                  readerInkWeightVersion=1, readerInkWeight=0 if warm else level,
                                  textSpacingVersion=3, paragraphIndentVersion=1,
                                  dropCapMode=0, textAntiAliasing=1, paragraphIndent=0,
                                  readerTapTip=0, wakeIntoBook=1, statusBarClock=0, statusBarTitle=0,
                                  statusBarBattery=0, statusBarChapterPageCount=0, sleepTimeout=120))
        (store / "settings.json").write_text(json.dumps(settings))
        (store / "state.json").write_text(json.dumps(dict(showBootScreen=False, openEpubPath="/ink.epub",
                                                          lastSleepFromReader=True)))
        (store / "recent.json").write_text(json.dumps({"books": [{"path": "/ink.epub", "title": "Ink fixture"}]}))
        script, capture = "3500:QUIT", 2500
        if warm and level:
            assert board == "pro", "warm path exercises the X4 Pro touch menu"
            actions = ["2500:TAP:240,775", "3500:SWIPE:240,600,240,250,120", "4500:SWIPE:240,600,240,250,120"]
            actions.extend("%d:TAP:240,619" % (5500 + i * 1000) for i in range(level))
            close = 5500 + level * 1000
            actions.extend(["%d:TAP:46,754" % close, "%d:QUIT" % (close + 2500)])
            script, capture = ";".join(actions), close + 1500
        env = {key: value for key, value in os.environ.items() if not key.startswith("CROSSPOINT_SIM_")}
        env.update(SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(case),
                   CROSSPOINT_SIM_WAKE_REASON="power",
                   CROSSPOINT_SIM_INPUT_SCRIPT=script,
                   CROSSPOINT_SIM_SCREENSHOTS=str(capture) + ":" + str(case / "page.bmp"))
        run = subprocess.run([str(program.resolve())], cwd=REPO, env=env, capture_output=True, text=True, timeout=40)
        log = run.stdout + run.stderr
        (case / "run.log").write_text(log)
        assert run.returncode == 0 and "Entering activity: EpubReader" in log, log[-4000:]
        if not pack:
            assert "Loaded TTF font: " + family in log, "fixture must use the vector reader font"
            assert "Auto hinting unavailable" not in log, "vector fixture requires production auto-hinting"
        if not warm:
            assert "Entering activity: EpubReaderMenu" not in log, "capture must stay on the reader page"
        assert json.loads((store / "settings.json").read_text())["readerInkWeight"] == level
        with Image.open(case / "page.bmp") as raw:
            image = raw.convert("RGB")
            if image.width > image.height:
                image = image.rotate(90, expand=True)
            assert image.size == ((528, 792) if board == "x3" else (480, 800))
            image.save(case / "page.png")
        measured = bounds(image)
        assert measured["body_bbox"], "reader body is empty"
        measurements.append(dict(level=level, **measured))
    report = dict(program_sha256=hashlib.sha256(program.read_bytes()).hexdigest(), measurements=measurements)
    (output / "measurements.json").write_text(json.dumps(report, indent=2))
    hashes = [item["body_sha256"] for item in measurements]
    for item in measurements:
        print("level=%d coverage=%d bw_pixels=%d sha=%s" %
              (item["level"], item["coverage"], item["bw_pixels"], item["body_sha256"]))
    assert len(set(hashes)) == 4, "four public ink levels must change actual reader pixels"
    assert all(a["coverage"] < b["coverage"] for a, b in zip(measurements, measurements[1:])), "ink coverage must grow"
    print("PASS four ink levels change the reader body")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--program", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--pack", type=Path)
    parser.add_argument("--board", choices=("pro", "x3"), default="pro")
    parser.add_argument("--warm", action="store_true", help="change ink through the reader text menu")
    args = parser.parse_args()
    check(args.program, args.output, args.pack, args.board, args.warm)
