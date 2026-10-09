"""Shared X4 Pro page scrollbars: image geometry, idle expiry and input."""

import argparse
import json
import re
from pathlib import Path

from PIL import Image, ImageChops

from test_thanh_day import run


def dark(image, x, y):
    return image.getpixel((x, y)) < 128


def thumb(image, top, bottom):
    rows = [y for y in range(top, bottom)
            if sum(dark(image, x, y) for x in range(469, 475)) >= 4]
    return (min(rows), max(rows) + 1) if rows else None


def check_geometry(image, top, bottom, content_height=None, at_end=False, offset=None, background=None):
    if background is not None:
        image = ImageChops.invert(ImageChops.difference(image, background))
    bar = thumb(image, top, bottom)
    assert bar, "missing 6 px scrollbar outside the group frames"
    columns = [x for x in range(464, 480)
               if any(dark(image, x, y) for y in range(top, bottom))]
    assert columns == list(range(469, 475)), f"bar columns={columns}"
    inner_rows = [y for y in range(top, bottom)
                  if sum(dark(image, x, y) for x in range(452, 458)) >= 4
                  and not dark(image, 451, y) and not dark(image, 458, y)]
    longest = current = 0
    previous = -2
    for row in inner_rows:
        current = current + 1 if row == previous + 1 else 1
        longest = max(longest, current)
        previous = row
    assert longest <= 12, f"another scrollbar remains inside a frame, run={longest}"
    assert not any(dark(image, x, y) for y in range(top, bottom) for x in range(464, 469)), \
        "bar crossed the 5 px gutter beside the frame"
    for label, outside in (("above", range(32, top)), ("below", range(bottom, 724))):
        run_length = 0
        for row in outside:
            run_length = run_length + 1 if all(dark(image, column, row) for column in range(469, 475)) else 0
            assert run_length <= 2, f"bar {label} its viewport"
    if content_height:
        minimum = (bottom - top) ** 2 / content_height - 2
        assert bar[1] - bar[0] >= minimum, f"thumb={bar}, required length >= {minimum:.2f}"
        if offset is not None:
            visible = bottom - top
            height = visible * visible // content_height
            position = top + (visible - height) * min(offset, content_height - visible) // (content_height - visible)
            assert abs(bar[0] - position) <= 2, f"thumb y={bar[0]}, proportional position={position}"
    widths = [sum(dark(image, x, y) for x in range(469, 475)) for y in range(bar[0], bar[1])]
    assert widths[0] < max(widths) and widths[-1] < max(widths), "thumb ends are square"
    assert all(width >= 4 for width in widths), "more than 1 separate thumb on the track"
    if at_end:
        assert bar[1] >= bottom - 1, f"end thumb={bar}, viewport bottom={bottom}"
    return bar


def difference_outside(before, after, box):
    diff = ImageChops.difference(before, after)
    diff.paste(0, box)
    return diff.getbbox()


def home_case(output, tier):
    folder = output / f"home-{tier}"
    bottom = 716 - (0, 7, 12)[tier]
    script = ("3000:TAP:455,754;4200:SWIPE:240,600,240,520,650;"
              "5700:SWIPE:240,600,240,520,650;7200:SWIPE:240,650,240,250,150;"
              "10300:SWIPE:240,300,240,640,150")
    images = run(folder, script, [3800, 5200, 6700, 7900, 9100, 9900, 11000],
                 settings={"uiTextSize": tier, "sleepTimeoutMinutes": 120})
    names = ["home", "cuon1", "cuon2", "cuon3", "before-hide", "hidden", "reshown"]
    for name, image in zip(names, images):
        image.save(folder / f"{name}.png")
    montage = Image.new("L", (480 * 4, 800), 255)
    for index, image in enumerate(images[:4]):
        montage.paste(image, (480 * index, 0))
    montage.save(folder / "ghep.png")
    failures = []
    bars = []
    content_height = (766, 784, 829)[tier]
    offsets = ((0, 99, 99, 99), (0, 105, 167, 167), (0, 113, 178, 178))[tier]
    for index, image in enumerate(images[:4]):
        try:
            bars.append(check_geometry(image, 48, bottom, content_height, index == 3, offsets[index]))
        except AssertionError as error:
            failures.append(f"{names[index]}: {error}")
    before, hidden = images[4:6]
    if not thumb(before, 48, bottom):
        failures.append("bar disappeared before 2000 ms")
    if any(dark(hidden, x, y) for y in range(48, bottom) for x in range(469, 475)):
        failures.append("bar remained after 2000 ms")
    if difference_outside(before, hidden, (469, 48, 475, bottom)):
        failures.append("idle hide changed pixels outside its narrow strip")
    if ImageChops.difference(before, hidden).getbbox() is None:
        failures.append("idle hide did not change any pixels")
    if not thumb(images[6], 48, bottom):
        failures.append("scroll after idle did not restore the bar")
    windows = (folder / "simulator.log").read_text().count("Window logical=")
    if windows != 1:
        failures.append(f"idle should use 1 narrow refresh, got {windows}")
    full_updates = re.findall(r"\[(\d+)\].*Time = .*displayBuffer", (folder / "simulator.log").read_text())
    if any(9100 < int(at) < 9900 for at in full_updates):
        failures.append("idle hide also did a full-screen refresh")
    return {"case": f"home-{tier}", "thumbs": bars, "failures": failures}


def stats_case(output, tier):
    folder = output / f"stats-{tier}"
    bottom = 716 - (0, 7, 12)[tier]
    script = ("3000:TAP:331,754;4200:SWIPE:240,640,240,300,150;"
              "5500:SWIPE:240,640,240,300,150;6800:SWIPE:240,640,240,300,150")
    images = run(folder, script, [3800, 7600, 9500], settings={"uiTextSize": tier})
    failures = []
    for label, image in zip(("home", "end", "hidden"), images):
        image.save(folder / f"{label}.png")
    for index in range(2):
        try:
            check_geometry(images[index], 32, bottom, (841, 894, 966)[tier], at_end=index == 1,
                           offset=0 if index == 0 else None)
        except AssertionError as error:
            failures.append(str(error))
    if thumb(images[2], 32, bottom):
        failures.append("Stats bar did not hide")
    if difference_outside(images[1], images[2], (469, 32, 475, bottom)):
        failures.append("Stats hide changed content pixels")
    return {"case": f"stats-{tier}", "failures": failures}


def touch_during_hide(output):
    folder = output / "touch-during-hide"
    (image,) = run(folder, "3000:TAP:455,754;5300:TAP:240,303", [6900],
                   sim_env={"CROSSPOINT_SIM_WINDOW_MS": "700", "CROSSPOINT_SIM_STRICT_SAMPLING": "1"})
    image.save(folder / "after-touch.png")
    log = (folder / "simulator.log").read_text()
    begin = re.search(r"WINDOW begin ms=(\d+)", log)
    end = re.search(r"WINDOW end ms=(\d+)", log)
    failures = []
    if not begin or not end or not (int(begin[1]) < 5300 < int(end[1])):
        failures.append("test tap did not land during the narrow refresh")
    if "Entering activity: Settings" not in log:
        failures.append("tap during hide did not open Control settings")
    if "lost press" in log:
        failures.append("strict sampling dropped a touch")
    return {"case": "touch-during-hide", "failures": failures}


def resume_case(output):
    folder = output / "resume"
    images = run(folder, "3000:TAP:455,754;6000:TAP:240,303;7900:TAP:46,754",
                 [3800, 5500, 6900, 8700, 10400])
    for name, image in zip(("entered", "hidden", "child", "returned", "hidden-again"), images):
        image.save(folder / f"{name}.png")
    failures = []
    if not thumb(images[0], 48, 716) or thumb(images[1], 48, 716):
        failures.append("first screen entry/idle state is wrong")
    if not thumb(images[3], 48, 716):
        failures.append("returning to Settings did not show the bar")
    if thumb(images[4], 48, 716):
        failures.append("returned screen did not hide again")
    return {"case": "resume", "failures": failures}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--home-only", action="store_true")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    results = [home_case(args.output, tier) for tier in range(3)]
    if not args.home_only:
        results += [stats_case(args.output, tier) for tier in range(3)]
        results.append(touch_during_hide(args.output))
        results.append(resume_case(args.output))
    (args.output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    for result in results:
        print("RED" if result["failures"] else "GREEN", result)
    assert not any(result["failures"] for result in results), results


if __name__ == "__main__":
    main()
