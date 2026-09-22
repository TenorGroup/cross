"""UI tier acceptance on a frozen X3 simulator and its real framebuffer.

TEST_PROGRAM and TEST_PROGRAM_SHA256 are required to pin the reviewed binary.
The matrix covers 3 tiers x VI/EN/ZH_HANS. Image bounds and selected row pixels
are measured, while visual glyph collision acceptance remains human review.
"""

import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from PIL import Image, ImageChops

REPO = Path(__file__).resolve().parents[2]
ART = Path(os.environ.get("CROSSPOINT_TEST_ARTIFACTS", REPO.parent / "research/ui-layout/simulator"))
BODY_LINES = (33, 38, 43)
FOOTERS = (68, 80, 90)
TITLES = {
    "VI": "Những ngày bình yên giữa thành phố rộng lớn",
    "EN": "Quiet days in a very large and crowded city",
    # The built-in font ships the UI CJK subset. Arbitrary book characters
    # require an SD font; use covered glyphs to isolate layout acceptance.
    "ZH_HANS": "阅读时间和最近读过的书 阅读时间和最近读过的书",
}


class Timeline:
    def __init__(self):
        self.time = 1000
        self.events = []
        self.shots = []

    def key(self, button, count=1):
        for _ in range(count):
            self.events.append(f"{self.time}:{button}")
            self.time += 700
        return self

    def shot(self, name):
        self.shots.append((self.time - 200, name))
        return self

    def end(self):
        self.events.append(f"{self.time + 300}:QUIT")
        return ";".join(self.events)


def selected_band(image, top=125, bottom=700):
    """Longest mostly black list-row fill, excluding header/footer surfaces."""
    gray = image.convert("L")
    pixels = gray.load()
    # Sample the inner left fill before glyphs begin. A wide scan splits the
    # band at long white labels and reports a fraction of its true height.
    xs = range(26, 30)
    bands = []
    start = None
    for y in range(top, min(bottom, image.height)):
        filled = sum(pixels[x, y] < 100 for x in xs) > len(xs) * 0.65
        if filled and start is None:
            start = y
        elif not filled and start is not None:
            bands.append((start, y))
            start = None
    if start is not None:
        bands.append((start, bottom))
    return max(bands, key=lambda b: b[1] - b[0]) if bands else None


def selected_text_ink(image, band):
    """Measure real white glyph ink inside a black selected row, in label area."""
    crop = image.crop((42, band[0] + 3, 270, band[1] - 3)).convert("L")
    return crop.point(lambda p: 255 if p > 180 else 0).getbbox()


class UiSizesV108Test(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        program = os.environ.get("TEST_PROGRAM")
        expected = os.environ.get("TEST_PROGRAM_SHA256")
        if not program or not expected:
            raise RuntimeError("Set TEST_PROGRAM and TEST_PROGRAM_SHA256 to the frozen simulator")
        cls.program = Path(program).resolve()
        cls.sha = hashlib.sha256(cls.program.read_bytes()).hexdigest()
        if cls.sha != expected:
            raise RuntimeError(f"Frozen simulator SHA mismatch: {cls.sha} != {expected}")
        ART.mkdir(parents=True, exist_ok=True)

    def fixture(self, tier, locale, name):
        temporary = tempfile.TemporaryDirectory(prefix="cross-ui108-")
        self.addCleanup(temporary.cleanup)
        sd = Path(temporary.name)
        store = sd / ".crosspoint"
        store.mkdir()
        settings = dict(language=locale, uiTheme=4, uiTextSize=tier, fontSize=14,
                        sleepTimeout=120, globalStatusBarMode=0, clockHasBeenSynced=1,
                        clockUtcOffsetQ=48, deviceName="Reader-ABCDEFGHIJKLMNOPQR",
                        paragraphIndentVersion=1, textSpacingVersion=3, textAntiAliasing=0)
        (store / "settings.json").write_text(json.dumps(settings))
        (store / "state.json").write_text(json.dumps(dict(openEpubPath="", lastSleepFromReader=False,
                                                         showBootScreen=False, readerActivityLoadCount=0)))
        books = []
        for i in range(5):
            path = f"book{i}.txt"
            (sd / path).write_text(("Reading the same passage keeps the reader font independent. " * 40 + "\n") * 8)
            books.append(dict(path="/" + path, title=TITLES[locale] + f" {i + 1}", author="Tenor",
                              coverBmpPath="", excerpt=TITLES[locale] + ". " + TITLES[locale]))
        (store / "recent.json").write_text(json.dumps(dict(books=books), ensure_ascii=False))
        today = datetime.datetime.now(datetime.timezone.utc).date()
        days = [[int((today - datetime.timedelta(days=i)).strftime("%Y%m%d")),
                 12 + i, 30 + i] for i in range(6, -1, -1)]
        (store / "reading-stats.json").write_text(json.dumps(dict(schema=4, ngay=days)))
        output = ART / name
        output.mkdir(parents=True, exist_ok=True)
        return sd, output

    def run_sim(self, sd, output, name, timeline):
        script = timeline.end()
        env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}
        env.update(SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT=script,
                   CROSSPOINT_SIM_SCREENSHOTS=";".join(f"{ms}:{output / (name + '-' + label + '.bmp')}"
                                                         for ms, label in timeline.shots))
        result = subprocess.run([str(self.program)], cwd=REPO, env=env, capture_output=True,
                                text=True, timeout=timeline.time / 1000 + 30)
        self.assertEqual(hashlib.sha256(self.program.read_bytes()).hexdigest(), self.sha,
                         "Simulator binary changed during acceptance")
        log = result.stdout + result.stderr
        (output / (name + ".log")).write_text(log)
        (output / (name + "-input.txt")).write_text(script)
        self.assertEqual(result.returncode, 0, log[-5000:])
        self.assertIn("Entering activity: Home", log)
        images = {}
        for _, label in timeline.shots:
            with Image.open(output / (name + "-" + label + ".bmp")) as raw:
                im = raw.convert("RGB")
            if im.width > im.height:
                im = im.rotate(90, expand=True)
            self.assertEqual(im.size, (528, 792), f"Unexpected X3 framebuffer: {im.size}")
            self.assertIsNotNone(ImageChops.difference(im, Image.new("RGB", im.size, "white")).getbbox())
            im.save(output / (name + "-" + label + ".png"))
            images[label] = im
        return images, log

    def assert_row(self, image, tier, top=125):
        band = selected_band(image, top=top, bottom=792 - FOOTERS[tier])
        self.assertIsNotNone(band, "Selected row is missing from framebuffer")
        self.assertGreaterEqual(band[1] - band[0], BODY_LINES[tier], band)
        # Bands are half-open. Ending at footer_start leaves every footer pixel free.
        self.assertLessEqual(band[1], 792 - FOOTERS[tier], "Selected row overlaps footer")
        return band

    def matrix(self, tier, locale):
        name = f"{locale.lower()}-{tier}"
        sd, output = self.fixture(tier, locale, name)
        t = Timeline().shot("home")
        t.key("RIGHT", 4).shot("home-last")
        t.key("DOWN").shot("files").key("DOWN").shot("favorites")
        t.key("DOWN")
        if tier:
            t.key("LEFT")  # enlarged stats retains original first action at row2
        t.shot("stats-page0")
        if tier:
            t.key("CONFIRM").shot("stats-page1")
            t.key("CONFIRM").shot("stats-return")
            t.key("RIGHT")
        t.key("CONFIRM").shot("habits").key("BACK")
        t.key("DOWN").shot("home-settings").key("RIGHT").key("CONFIRM").shot("display")
        t.key("CONFIRM").shot("popup").key("RIGHT").shot("popup-next").key("LEFT").key("CONFIRM")
        t.key("LEFT").shot("display-last").key("RIGHT")
        t.key("DOWN", 5).shot("device").key("RIGHT").key("CONFIRM").shot("keyboard")
        t.key("RIGHT").key("CONFIRM").shot("keyboard-edit").key("BACK").shot("keyboard-return")
        t.key("UP", 3).key("CONFIRM").shot("text-settings")
        t.key("LEFT").shot("text-settings-last").key("BACK")
        images, log = self.run_sim(sd, output, "matrix", t)
        self.assertIn("Entering activity: ReadingHabits", log)
        self.assertIn("Entering activity: Settings", log)
        self.assertIn("Entering activity: KeyboardEntry", log)
        self.assertIn("Exiting activity: KeyboardEntry", log)
        self.assertIn("Entering activity: TextSettings", log)
        measured = {}
        for label in ("home-last", "stats-page0", "display", "display-last", "device", "keyboard-return",
                      "text-settings", "text-settings-last"):
            try:
                measured[label] = self.assert_row(images[label], tier, top=480 if label == "home-last" else 125)
            except AssertionError as error:
                raise AssertionError(f"{locale} tier{tier} {label}: {error}") from error
        glyphs = selected_text_ink(images["display"], measured["display"])
        self.assertIsNotNone(glyphs, "Selected setting label has no visible white glyph ink")
        if tier:
            measured["stats-page1"] = self.assert_row(images["stats-page1"], tier)
            self.assertIsNotNone(ImageChops.difference(images["stats-page0"], images["stats-page1"]).getbbox())
            self.assertIsNone(ImageChops.difference(images["stats-page0"].crop((0, 120, 528, 690)),
                                                    images["stats-return"].crop((0, 120, 528, 690))).getbbox())
        self.assertIsNotNone(ImageChops.difference(images["popup"], images["popup-next"]).getbbox())
        self.assertIsNotNone(ImageChops.difference(images["keyboard"], images["keyboard-edit"]).getbbox())
        saved = json.loads((sd / ".crosspoint/settings.json").read_text())
        self.assertEqual(saved["uiTextSize"], tier)
        self.assertEqual(saved["uiTheme"], 4)
        self.assertEqual(saved["fontSize"], 14)
        self.assertTrue(saved["deviceName"].startswith("Reader-ABCDEFGHIJKLMNOPQR"))
        self.assertEqual(len(saved["deviceName"]), len("Reader-ABCDEFGHIJKLMNOPQR") + 1)
        (output / "measurements.json").write_text(json.dumps(dict(program=str(self.program), sha256=self.sha,
            locale=locale, tier=tier, selected_bands=measured, display_label_ink_bbox=glyphs, screenshots=len(images),
            scope="X3 physical button navigation and actual framebuffer; touch not exercised"), indent=2))

    def test_matrix_vi_0(self):
        self.matrix(0, "VI")

    def test_matrix_vi_1(self):
        self.matrix(1, "VI")

    def test_matrix_vi_2(self):
        self.matrix(2, "VI")

    def test_matrix_en_0(self):
        self.matrix(0, "EN")

    def test_matrix_en_1(self):
        self.matrix(1, "EN")

    def test_matrix_en_2(self):
        self.matrix(2, "EN")

    def test_matrix_zh_hans_0(self):
        self.matrix(0, "ZH_HANS")

    def test_matrix_zh_hans_1(self):
        self.matrix(1, "ZH_HANS")

    def test_matrix_zh_hans_2(self):
        self.matrix(2, "ZH_HANS")

    def test_cycle_immediate_restart_and_reader_independence(self):
        sd, output = self.fixture(0, "VI", "cycle")
        reference = None
        bands = []
        glyph_heights = []
        for tier in (1, 2, 0):
            # Each process starts at Home. A single Confirm cycles the size row.
            t = Timeline().key("UP").key("RIGHT").key("CONFIRM").key("RIGHT").shot("before")
            t.key("CONFIRM").shot("after").key("BACK").key("DOWN").shot("home-after")
            images, log = self.run_sim(sd, output, f"set-{tier}", t)
            self.assertIn("Exiting activity: Settings", log)
            bands.append(self.assert_row(images["after"], tier))
            glyphs = selected_text_ink(images["after"], bands[-1])
            self.assertIsNotNone(glyphs)
            glyph_heights.append(glyphs[3] - glyphs[1])
            self.assertIsNotNone(ImageChops.difference(images["before"], images["after"]).getbbox())
            saved = json.loads((sd / ".crosspoint/settings.json").read_text())
            self.assertEqual(saved["uiTextSize"], tier)
            self.assertEqual(saved["fontSize"], 14)
            t = Timeline().shot("home").key("CONFIRM")
            t.time += 2300
            t.shot("reader").key("BACK")
            restart, log = self.run_sim(sd, output, f"restart-{tier}", t)
            self.assertIn("Entering activity: TxtReader", log)
            self.assertIsNone(ImageChops.difference(images["home-after"].crop((0, 0, 528, 700)),
                                                    restart["home"].crop((0, 0, 528, 700))).getbbox())
            body = restart["reader"].crop((0, 0, 528, 650))
            if reference is None:
                reference = body
            else:
                self.assertIsNone(ImageChops.difference(reference, body).getbbox(),
                                  "UI size changed the fixed reader body pixels")
        self.assertLess(bands[2][1] - bands[2][0], bands[0][1] - bands[0][0])
        self.assertLess(bands[0][1] - bands[0][0], bands[1][1] - bands[1][0])
        self.assertLess(glyph_heights[2], glyph_heights[0])
        self.assertLess(glyph_heights[0], glyph_heights[1])
        (output / "measurements.json").write_text(json.dumps(dict(sha256=self.sha, sequence=[1, 2, 0],
            selected_bands=bands, selected_label_glyph_heights=glyph_heights,
            reader_crop=[0, 0, 528, 650], reader_pixels_identical=True), indent=2))


if __name__ == "__main__":
    unittest.main()
