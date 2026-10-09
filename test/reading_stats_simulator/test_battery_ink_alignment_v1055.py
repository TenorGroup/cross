import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from PIL import Image

REPO = Path(__file__).resolve().parents[2]


def ink_bounds(image, box, text=False):
    rows = [row for row in range(box[1], box[3])
            if any(image.getpixel((column, row)) < 128 for column in range(box[0], box[2]))]
    assert rows, f"missing ink in {box}"
    if text:
        runs = []
        for row in rows:
            if not runs or row != runs[-1][-1] + 1:
                runs.append([])
            runs[-1].append(row)
        rows = max(runs, key=len)
    return min(rows), max(rows)


def capture(program, folder, board, screen, charging, hidden, large):
    sd = folder / "sd"
    store = sd / ".crosspoint"
    store.mkdir(parents=True)
    settings = {"language": "VI", "tenorPresetVersion": 1, "sleepTimeout": 120,
                "hideBatteryPercentage": 2 if hidden else 0, "statusBarClock": 1,
                "globalStatusBarMode": 2 if large else 0, "readerStatusBarMode": 1, "statusBarItemsMode": 1,
                "readerStatusSlotsEnabled": int(screen == "slots"), "readerStatusLeft": 1,
                "readerStatusCenter": 0, "readerStatusRight": 2, "readerTapTip": 0, "wakeIntoBook": 1}
    (store / "settings.json").write_text(json.dumps(settings))
    reading = screen in ("reader", "slots", "reader-menu")
    book_path = "/alignment.epub" if screen == "reader-menu" else "/alignment.txt"
    (sd / "alignment.txt").write_text("Alignment fixture.\n" * 3)
    if screen == "reader-menu":
        sys.path.insert(0, str(REPO / "test/x4pro_simulator"))
        from test_reader_menu_round3 import write_book
        write_book(sd / "alignment.epub")
    if screen == "folder":
        (sd / "fixture").mkdir()
        (sd / "fixture" / "alignment.txt").write_text("Alignment fixture.\n")
    (store / "state.json").write_text(json.dumps({"showBootScreen": False,
                                                 "openEpubPath": book_path if reading else "",
                                                 "lastSleepFromReader": reading}))
    (store / "recent.json").write_text(json.dumps({"books": [{"path": book_path, "title": "Alignment"}]}))
    script = ""
    if screen == "settings":
        script = "2800:TAP:455,754;" if board == "x4pro" else "2800:UP;"
    elif screen == "file":
        script = "2800:TAP:148,754;"
    elif screen == "folder":
        script = "2200:TAP:148,754;3000:TAP:240,68;"
    elif screen == "reader-menu":
        script = "2800:TAP:240,775;"
    shot = folder / "frame.bmp"
    env = {key: value for key, value in os.environ.items() if not key.startswith("CROSSPOINT_SIM_")}
    if os.environ.get("TEST_CLOCK_LIBRARY"):
        env["DYLD_INSERT_LIBRARIES"] = os.environ["TEST_CLOCK_LIBRARY"]
    env.update(SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_USB=str(int(charging)),
               CROSSPOINT_SIM_INPUT_SCRIPT=script + "4300:QUIT", CROSSPOINT_SIM_SCREENSHOTS=f"3800:{shot}")
    if reading:
        env["CROSSPOINT_SIM_WAKE_REASON"] = "power"
    result = subprocess.run([str(program)], cwd=REPO, env=env, capture_output=True, text=True, timeout=45)
    log = result.stdout + result.stderr
    (folder / "simulator.log").write_text(log)
    assert result.returncode == 0, log[-3000:]
    activity = "EpubReader" if screen == "reader-menu" else "TxtReader" if reading else "Home"
    assert f"Entering activity: {activity}" in log, log[-3000:]
    if screen == "folder":
        assert "Entering activity: FileBrowser" in log, log[-3000:]
    image = Image.open(shot).convert("L")
    image.save(folder / "frame.png")
    return image


def measure(image, board, screen, hidden, large):
    width, height = image.size
    top = board == "x4pro" and screen in ("home", "settings", "file", "folder", "reader-menu")
    lane_top, lane_bottom = (0, 42) if top else (height - 42, height)
    if screen == "slots" or top:
        columns = [column for column in range(width * 2 // 3, width - 7)
                   if any(image.getpixel((column, row)) < 128 for row in range(lane_top, lane_bottom))]
        runs = []
        for column in columns:
            if not runs or column != runs[-1][-1] + 1:
                runs.append([])
            runs[-1].append(column)
        candidates = [run for run in runs if len(run) >= 24]
        assert len(candidates) == 1, f"battery columns ambiguous: {runs}"
        left, right = candidates[0][0], candidates[0][-1] + 1
        battery_box = (left, lane_top, right, lane_bottom)
        number_box = ((right + 1, lane_top, width - 8, lane_bottom) if screen == "slots"
                      else (width * 2 // 3, lane_top, left - 1, lane_bottom))
        clock_box = (18 if top else 8, lane_top, 112, lane_bottom)
    else:
        battery_box = (8, lane_top, 40 if large else 34, lane_bottom)
        number_box = (44 if large else 38, lane_top, 85 if large else 80, lane_bottom)
        clock_box = (width - 90, lane_top, width - 8, lane_bottom)
    battery = ink_bounds(image, battery_box)
    clock = ink_bounds(image, clock_box, text=True)
    number = None if hidden else ink_bounds(image, number_box, text=True)
    clock_left = next(column for column in range(clock_box[0], clock_box[2])
                      if any(image.getpixel((column, row)) < 128 for row in range(clock[0], clock[1] + 1)))
    return {"battery": battery, "battery_x": [battery_box[0], battery_box[2] - 1], "number": number, "clock": clock,
            "clock_left": clock_left,
            "delta_clock": [battery[index] - clock[index] for index in (0, 1)],
            "delta_number": None if number is None else [battery[index] - number[index] for index in (0, 1)],
            "crop": (0, lane_top, width, lane_bottom)}


def run(program, board, output, measure_only=False, baseline=None):
    assert measure_only or not output.exists(), f"output already exists: {output}"
    results = []
    failed = 0
    screens = ("home", "settings", "file", "folder", "reader-menu", "reader", "slots") if board == "x4pro" else ("home", "settings", "reader", "slots")
    for screen in screens:
        for large in ((False, True) if screen == "home" else (False,)):
            for charging in (False, True):
                for hidden in (False, True):
                    name = f"{screen}-{'large' if large else 'small'}-usb{int(charging)}-hide{int(hidden)}"
                    folder = output / name
                    image = (Image.open(folder / "frame.png").convert("L") if measure_only else
                             capture(program.resolve(), folder, board, screen, charging, hidden, large))
                    result = measure(image, board, screen, hidden, large)
                    image.crop(result.pop("crop")).resize((image.width * 3, 126), Image.Resampling.NEAREST).save(folder / "strip-3x.png")
                    result.update(board=board, screen=screen, large=large, charging=charging, hidden=hidden)
                    results.append(result)
                    passed = result["delta_clock"] == [0, 0] and result["delta_number"] in (None, [0, 0])
                    if board == "x4pro" and screen in ("home", "settings", "file", "folder", "reader-menu"):
                        bounds = [result["battery"], result["clock"]]
                        if result["number"] is not None:
                            bounds.append(result["number"])
                        passed &= all(bottom == 28 and 14 <= bottom - top + 1 <= 16 for top, bottom in bounds)
                        first = next(row for row in range(32, image.height - 100)
                                     if any(image.getpixel((column, row)) < 128 for column in range(image.width)))
                        result["first_content_row"] = first
                        result["content_gap"] = first - 29
                        passed &= result["content_gap"] >= 3
                        corners = list(range(112)) + list(range(image.width * 2 // 3, image.width))
                        result["clear_below_corner_ink"] = all(image.getpixel((column, row)) >= 128
                                                              for row in range(29, 32) for column in corners)
                        passed &= result["clear_below_corner_ink"]
                    if baseline is not None:
                        before = Image.open(baseline / name / "frame.png").convert("L")
                        top = board == "x4pro" and screen in ("home", "settings", "file", "folder", "reader-menu")
                        crop = ((0, 32 if top else 0, image.width, image.height) if board == "x4pro"
                                else (0, image.height - 42, image.width, image.height))
                        result["unchanged_pixels"] = before.crop(crop).tobytes() == image.crop(crop).tobytes()
                        passed &= result["unchanged_pixels"]
                        if top:
                            before_bounds = measure(before, board, screen, hidden, large)
                            result["same_battery_x"] = result["battery_x"] == before_bounds["battery_x"]
                            result["same_clock_left"] = result["clock_left"] == before_bounds["clock_left"]
                            passed &= result["same_battery_x"] and result["same_clock_left"]
                    failed += not passed
                    print(f"{'GREEN' if passed else 'RED'} {board} {name}: {result}", flush=True)
    (output / "bounds.json").write_text(json.dumps(results, indent=2))
    print(f"{'RED' if failed else 'GREEN'}: {len(results)} cases, {failed} failed")
    return failed


class BatteryInkAlignment(unittest.TestCase):
    # The suite runs this file without arguments: X3 with the suite's frozen binary.
    def test_x3_battery_ink_matches_digits(self):
        output = Path(tempfile.mkdtemp(prefix="battery-ink-")) / "run"
        self.assertEqual(run(REPO / ".pio/build/simulator_x3_uc8279/program", "x3", output), 0)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--program", type=Path, required=True)
    parser.add_argument("--board", choices=("x3", "x4pro"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--measure-only", action="store_true")
    parser.add_argument("--baseline", type=Path)
    args = parser.parse_args()
    baseline = args.baseline.resolve() if args.baseline is not None else None
    raise SystemExit(int(bool(run(args.program, args.board, args.output.resolve(), args.measure_only, baseline))))


if __name__ == "__main__":
    if len(sys.argv) > 1:
        main()
    else:
        unittest.main()
