import argparse
import hashlib
import json
from pathlib import Path

from PIL import ImageChops

from test_thanh_day import PROGRAM, run


def save_images(folder, stamps, images):
    for stamp, image in zip(stamps, images):
        image.save(folder / f"{stamp}.png")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    common = {"uiShell": 0, "uiTextSize": 0, "homeButtonDoubleTapAction": 10}
    home = args.output / "home"
    home.mkdir()
    home_stamps = [7000, 9000, 11500, 14000]
    images = run(home, "3000:TAP:455,754;4500:TAP:240,305;6000:TAP:240,80;8000:TAP:240,142;"
                       "10000:TAP:240,577;12500:TAP:46,754", home_stamps, settings=common)
    save_images(home, home_stamps, images)
    parent, choices, chosen, back = images
    log = (home / "simulator.log").read_text()
    assert log.count("Entering activity: SettingsChoices") == 1
    assert ImageChops.difference(choices.crop((0, 720, 480, 800)),
                                chosen.crop((0, 720, 480, 800))).getbbox() is None
    assert sum(pixel < 128 for pixel in choices.crop((420, 625, 445, 651)).getdata()) > 15
    assert sum(pixel < 128 for pixel in chosen.crop((420, 565, 445, 591)).getdata()) > 15
    saved = json.loads((home / "sd/.crosspoint/settings.json").read_text())
    assert saved["homeButtonDoubleTapAction"] == 9
    assert ImageChops.difference(parent.crop((20, 118, 450, 168)),
                                back.crop((20, 118, 450, 168))).getbbox()
    popup = args.output / "popup"
    popup.mkdir()
    popup_stamps = [5500, 9000, 11500, 14500, 17000]
    images = run(popup, "3000:TAP:455,754;4500:TAP:240,180;8000:TAP:240,142;"
                        "10000:TAP:240,550;13000:TAP:240,142;15500:TAP:240,254",
                 popup_stamps, settings={**common, "globalStatusBarMode": 0})
    save_images(popup, popup_stamps, images)
    parent, choices, dismissed, reopened, chosen = images
    assert ImageChops.difference(parent.crop((0, 48, 480, 800)),
                                dismissed.crop((0, 48, 480, 800))).getbbox() is None
    assert ImageChops.difference(choices.crop((0, 48, 480, 800)),
                                reopened.crop((0, 48, 480, 800))).getbbox() is None
    rules = [row for row in range(48, 716)
             if sum(choices.getpixel((column, row)) < 128 for column in range(40, 440)) > 180]
    assert 108 in rules and 287 in rules, rules
    assert sum(pixel < 128 for pixel in choices.crop((420, 130, 445, 156)).getdata()) > 15
    assert not any(sum(choices.getpixel((column, row)) < 128 for column in range(452, 458)) >= 4
                   for row in range(125, 270))
    assert sum(pixel < 128 for pixel in parent.crop((429, 128, 450, 142)).getdata()) > 8
    assert sum(pixel < 128 for pixel in parent.crop((429, 143, 450, 156)).getdata()) > 8
    saved = json.loads((popup / "sd/.crosspoint/settings.json").read_text())
    assert saved["globalStatusBarMode"] == 2
    result = {"program_sha256": hashlib.sha256(PROGRAM.read_bytes()).hexdigest(),
              "home_options": 11, "home_saved": 9, "stays_on_choice_page": True,
              "popup_options": 3, "popup_height": 180, "row_height": 56,
              "selected_center_y": 142, "tapped_row_y": 142, "popup_scrollbar": False,
              "outside_tap_closes": True, "popup_saved": 2}
    (args.output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print("GREEN U12:", json.dumps(result))


if __name__ == "__main__":
    main()
