"""X4 Pro startup must restore exact vector family names from saved settings."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


REPO = Path(__file__).resolve().parents[2]
FONT = REPO / "lib/EpdFont/builtinFonts/source/BeVietnamPro/BeVietnamPro-Regular.ttf"
FAMILIES = ("BeVietnamPro", "SP3 - Traveling Typewriter-BOLD1", "SP3 - Traveling Typewriter-BOLD2")


def check(program, output):
    for family in FAMILIES:
        sd = output / family
        folder = sd / ".fonts" / family
        folder.mkdir(parents=True)
        shutil.copyfile(FONT, folder / "Regular.ttf")
        if family == FAMILIES[0]:
            for length in (63, 64):
                bounded = sd / ".fonts" / ("d" * length)
                bounded.mkdir()
                shutil.copyfile(FONT, bounded / "Regular.ttf")
                shutil.copyfile(FONT, sd / ".fonts" / (("f" * length) + ".ttf"))
        store = sd / ".crosspoint"
        store.mkdir()
        (store / "settings.json").write_text(json.dumps({
            "language": "EN", "fontSize": 18, "fontFamily": 0,
            "sdFontFamilyName": family, "tenorPresetVersion": 1,
            "textSpacingVersion": 3, "paragraphIndentVersion": 1,
            "readerInkWeightVersion": 1, "uiShellSleepMemo": 0,
        }))
        (store / "state.json").write_text(json.dumps({"showBootScreen": False}))
        env = {key: value for key, value in os.environ.items() if not key.startswith("CROSSPOINT_SIM_")}
        env.update(SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(sd),
                   CROSSPOINT_SIM_WAKE_REASON="power", CROSSPOINT_SIM_INPUT_SCRIPT="2500:QUIT")
        run = subprocess.run([str(program)], cwd=REPO, env=env, capture_output=True, text=True, timeout=20)
        log = run.stdout + run.stderr
        (sd / "run.log").write_text(log)
        saved = json.loads((store / "settings.json").read_text())
        assert run.returncode == 0, (family, run.returncode, log[-2000:])
        if "Found family: " + family not in log and "Loaded TTF font: " + family not in log:
            print("SKIP: simulator lacks vector font discovery (build simulator_x4pro_font)")
            return 77
        assert saved["sdFontFamilyName"] == family, (family, saved["sdFontFamilyName"], log[-2000:])
        assert f"Loaded TTF font: {family} @" in log, (family, log[-2000:])
        if family == FAMILIES[0]:
            assert "Found family: " + "d" * 63 + " (" in log
            assert "Found vector font: " + "f" * 63 + " in " in log
            assert "Found family: " + "d" * 64 + " (" not in log
            assert "Found vector font: " + "f" * 64 + " in " not in log
            print("PASS discovery boundary: folder and loose font 63/64 bytes")
        print(f"PASS {family}")
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--program", type=Path, default=REPO / ".pio/build/simulator_x4pro/program")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.output:
        args.output.mkdir(parents=True, exist_ok=True)
        sys.exit(check(args.program, args.output))
    else:
        with tempfile.TemporaryDirectory(prefix="x4pro-font-boot-") as folder:
            sys.exit(check(args.program, Path(folder)))
