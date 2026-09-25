"""File transfer opened from a book returns to that book, on the same page.

Journeys on the X3 simulator: open a book, move to chapter 2 page 2, open File
transfer from the reader menu (Tools tab) or with a Select hold, run the server
on a saved network, leave with Back. The reader must come back on the page it
left, proven by the loaded progress and by comparing the page image before and
after. Opened from Home, File transfer still returns to Home. A book removed
while the server runs sends the reader to Home instead of a missing file. A
sleep taken inside File transfer opened from a book counts as a sleep from the
reader, so the wake-into-book setting decides where the wake lands.

The simulator has no reboot: the firmware leaves a Wi-Fi session through a
silent restart, which the simulator logs and then skips. The logged restart
target proves which activity the X3 boots into; the reboot itself is checked
on the device.

Set CROSSPOINT_TRANSFER_RETURN_EVIDENCE to a directory to keep the screenshots and logs.
"""

import json
import os
import re
import shutil
import subprocess
import tempfile
import threading
import unittest
import zipfile
from pathlib import Path

from PIL import Image, ImageChops
from cai_dat_truoc_tenor import truoc_tenor

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get("TEST_PROGRAM", REPO / ".pio/build/simulator_x3_uc8279/program"))
BOOK = "/books/doc.epub"
SSID = "Mang Mau"
ENTERING = re.compile(r"Entering activity: (\S+)")
LOADED = re.compile(r"Loaded cache: (\d+), (\d+)")

PARA = ("Doan van mau cho bai kiem gui file, du dai de moi trang day chu. " * 7 + "\n") * 10

# Home: Select opens the recent book. A Right hold (chapter skip) moves to chapter 2,
# two Right taps move to its page 2.
TO_PAGE = ["1000:CONFIRM", "3000:RIGHT:1000", "5000:RIGHT", "6000:RIGHT"]
# Reader menu: Down steps tabs, three reach Tools. Left wraps to its last row (tilt), a
# second Left is File transfer, Select opens it.
MENU_TO_TRANSFER = ["8000:CONFIRM", "9200:DOWN", "9800:DOWN", "10400:DOWN", "11200:LEFT", "12000:LEFT",
                    "12800:CONFIRM"]
# Chooser: Select joins the saved network, the server runs, one Back leaves.
JOIN_THEN_BACK = ["14800:CONFIRM", "19000:BACK:80"]
BEFORE_SHOT = 7500
AFTER_SHOT = 23500


def write_epub(path: Path, chapters: int = 3) -> None:
    nav = "".join(f'<navPoint id="n{i}" playOrder="{i}"><navLabel><text>Chuong {i}</text></navLabel>'
                  f'<content src="c{i}.xhtml"/></navPoint>' for i in range(1, chapters + 1))
    spine = "".join(f'<itemref idref="c{i}"/>' for i in range(1, chapters + 1))
    manifest = "".join(f'<item id="c{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>'
                       for i in range(1, chapters + 1))
    with zipfile.ZipFile(path, "w") as epub:
        epub.writestr("mimetype", "application/epub+zip")
        epub.writestr("META-INF/container.xml",
                      '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" '
                      'version="1.0"><rootfiles><rootfile full-path="book.opf" '
                      'media-type="application/oebps-package+xml"/></rootfiles></container>')
        epub.writestr("book.opf",
                      '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" '
                      'unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
                      "<dc:title>Transfer fixture</dc:title><dc:identifier id=\"id\">transfer-return</dc:identifier>"
                      "<dc:language>vi</dc:language></metadata>"
                      f'<manifest>{manifest}<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>'
                      f'</manifest><spine toc="ncx">{spine}</spine></package>')
        epub.writestr("toc.ncx",
                      '<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">'
                      f"<head/><docTitle><text>Transfer fixture</text></docTitle><navMap>{nav}</navMap></ncx>")
        for i in range(1, chapters + 1):
            epub.writestr(f"c{i}.xhtml",
                          '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head>'
                          f"<title>Chuong {i}</title></head><body><h2>Chuong {i}</h2>" +
                          "".join(f"<p>{i}.{k} {PARA}</p>" for k in range(6)) + "</body></html>")


class FileTransferReturnTest(unittest.TestCase):
    maxDiff = None

    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix="cross-transfer-return-")
        self.addCleanup(tmp.cleanup)
        self.sd = Path(tmp.name)
        self.store = self.sd / ".crosspoint"
        self.store.mkdir()
        (self.sd / "books").mkdir()
        write_epub(self.sd / BOOK.lstrip("/"))
        (self.store / "recent.json").write_text(json.dumps({"books": [{"path": BOOK, "title": "Transfer fixture"}]}))
        (self.store / "wifi.json").write_text(json.dumps({"lastConnectedSsid": SSID, "credentials": [{"ssid": SSID}]}))
        (self.store / "state.json").write_text(json.dumps({"openEpubPath": "", "lastSleepFromReader": False,
                                                           "showBootScreen": False}))
        self.settings(longPressButtonBehavior=1)

    def settings(self, **fields):
        data = {"language": "VI", "sleepTimeout": 10, "globalStatusBarMode": 0}
        data.update(fields)
        (self.store / "settings.json").write_text(json.dumps(truoc_tenor(data)))

    def run_sim(self, name, events, shots=(), end=None, env_extra=None, during=None):
        """`events` are 'ms:KEY' or 'ms:KEY:holdms'; `shots` are (ms, name); `during` is (seconds, fn)."""
        end = end or int(events[-1].split(":")[0]) + 4000
        env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}
        env.update(SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(self.sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT=";".join([*events, f"{end}:QUIT"]))
        if shots:
            env["CROSSPOINT_SIM_SCREENSHOTS"] = ";".join(f"{ms}:{self.sd / (shot + '.bmp')}" for ms, shot in shots)
        env.update(env_extra or {})
        proc = subprocess.Popen([str(PROGRAM)], cwd=REPO, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        timer = None
        if during:
            timer = threading.Timer(during[0], during[1])
            timer.start()
        try:
            out, _ = proc.communicate(timeout=end / 1000 + 60)
        finally:
            if timer:
                timer.cancel()
        (self.sd / f"{name}.log").write_text(out)
        self.keep([f"{shot}.bmp" for _, shot in shots] + [f"{name}.log"])
        self.assertEqual(proc.returncode, 0, out[-6000:])
        return out

    def keep(self, names):
        evidence = os.environ.get("CROSSPOINT_TRANSFER_RETURN_EVIDENCE")
        if not evidence:
            return
        target = Path(evidence)
        target.mkdir(parents=True, exist_ok=True)
        for file in names:
            if (self.sd / file).exists():
                shutil.copy2(self.sd / file, target / file)

    def shot(self, name):
        with Image.open(self.sd / f"{name}.bmp") as image:
            return image.convert("L")

    def same_page(self, before, after):
        """Pixel compare of the page body. The status bar is masked: its clock follows the
        host's wall clock, and its chapter page total is an estimate refined once the
        reopened chapter finishes its page build, the same on a plain reopen from Home."""
        a, b = self.shot(before), self.shot(after)
        status_bar = (0, a.height - 40, a.width, a.height)
        a.paste(255, status_bar)
        b.paste(255, status_bar)
        return ImageChops.difference(a, b).getbbox()

    @staticmethod
    def after_transfer(log):
        """Activities entered after File transfer closed."""
        exited = log.split("Exiting activity: CrossPointWebServer", 1)
        return ENTERING.findall(exited[1]) if len(exited) == 2 else []

    def saved_positions(self, log):
        return [(int(s), int(p)) for s, p in re.findall(r"Progress saved: spine=(\d+) offset=\d+ page=(\d+)", log)]

    def assert_back_on_page(self, log, before, after):
        self.assertIn("Network mode: STA", log, log[-4000:])
        entered = self.after_transfer(log)
        self.assertTrue(entered, f"File transfer never closed\n{log[-4000:]}")
        self.assertEqual(entered[0], "EpubReader", f"left File transfer for {entered}\n{log[-4000:]}")
        self.assertNotIn("Home", entered, log[-4000:])
        self.assertIn("Silent restart (target=reader)", log, log[-4000:])
        reopened = log.split("Exiting activity: CrossPointWebServer", 1)[1]
        self.assertEqual(LOADED.findall(reopened)[:1], [("1", "2")], reopened[-3000:])
        self.assertEqual(self.saved_positions(reopened)[-1:], [(1, 2)], reopened[-3000:])
        # The same page, pixel for pixel: chapter, page and layout all came back.
        diff = self.same_page(before, after)
        self.assertIsNone(diff, f"page image differs in {diff}")

    def test_1_reader_menu_transfer_back_returns_to_same_page(self):
        log = self.run_sim("menu", [*TO_PAGE, *MENU_TO_TRANSFER, *JOIN_THEN_BACK],
                           shots=[(BEFORE_SHOT, "1-before"), (12600, "1-tools-row"), (18500, "1-server"),
                                  (AFTER_SHOT, "1-after")], end=AFTER_SHOT + 1000)
        self.assertEqual(self.saved_positions(log.split("Entering activity: CrossPointWebServer", 1)[0])[-1:],
                         [(1, 2)], log[-4000:])
        self.assert_back_on_page(log, "1-before", "1-after")

    def test_2_select_hold_transfer_back_returns_to_same_page(self):
        self.settings(longPressButtonBehavior=1, longPressMenuFunction=5)
        log = self.run_sim("hold", [*TO_PAGE, "8000:CONFIRM:900", *JOIN_THEN_BACK],
                           shots=[(BEFORE_SHOT, "2-before"), (AFTER_SHOT, "2-after")], end=AFTER_SHOT + 1000)
        self.assertNotIn("Entering activity: EpubReaderMenu", log)
        self.assert_back_on_page(log, "2-before", "2-after")

    def test_3_back_from_chooser_returns_to_book(self):
        # No Wi-Fi was started: the chooser's own Back leaves File transfer.
        log = self.run_sim("chooser", [*TO_PAGE, *MENU_TO_TRANSFER, "14800:BACK:80"],
                           shots=[(BEFORE_SHOT, "3-before"), (19000, "3-after")], end=20000)
        self.assertNotIn("Network mode: STA", log)
        entered = self.after_transfer(log)
        self.assertEqual(entered[:1], ["EpubReader"], f"left File transfer for {entered}\n{log[-4000:]}")
        diff = self.same_page("3-before", "3-after")
        self.assertIsNone(diff, f"page image differs in {diff}")

    def test_4_from_home_back_returns_home(self):
        # Home: the Settings tab is three Right from Recent; its top row is File transfer.
        log = self.run_sim("home", ["1500:RIGHT", "1900:RIGHT", "2300:RIGHT", "2700:UP", "3100:CONFIRM",
                                    "4400:CONFIRM", "9000:BACK:80"],
                           shots=[(12000, "4-home")], end=13000)
        self.assertIn("Network mode: STA", log, log[-4000:])
        entered = self.after_transfer(log)
        self.assertEqual(entered[:1], ["Home"], f"left File transfer for {entered}\n{log[-4000:]}")
        self.assertNotIn("EpubReader", entered)
        self.assertIn("Silent restart (target=home)", log, log[-4000:])

    def test_5_book_removed_during_transfer_goes_home(self):
        book = self.sd / BOOK.lstrip("/")
        # Removed while the server runs (about 17 s into the run), before Back at 19 s.
        log = self.run_sim("removed", [*TO_PAGE, *MENU_TO_TRANSFER, *JOIN_THEN_BACK],
                           shots=[(AFTER_SHOT, "5-home")], end=AFTER_SHOT + 1000,
                           during=(17.5, lambda: book.unlink()))
        self.assertFalse(book.exists())
        entered = self.after_transfer(log)
        self.assertEqual(entered[:1], ["Home"], f"left File transfer for {entered}\n{log[-4000:]}")
        self.assertNotIn("EpubReader", entered)
        self.assertIn("Silent restart (target=home)", log, log[-4000:])

    def test_6_sleep_inside_transfer_from_book_wakes_by_setting(self):
        # Sleep at the chooser. The sleep counts as a sleep from the reader.
        self.run_sim("sleep", [*TO_PAGE, *MENU_TO_TRANSFER, "15000:SLEEP"], end=19000)
        state = json.loads((self.store / "state.json").read_text())
        self.assertEqual((state["openEpubPath"], state["lastSleepFromReader"]), (BOOK, True), state)
        for wake_into_book, expected in ((1, "EpubReader"), (0, "Home")):
            with self.subTest(wakeIntoBook=wake_into_book):
                self.settings(longPressButtonBehavior=1, wakeIntoBook=wake_into_book)
                (self.store / "state.json").write_text(json.dumps(state))
                log = self.run_sim(f"wake-{wake_into_book}", ["4000:QUIT"], end=4000,
                                   env_extra={"CROSSPOINT_SIM_WAKE_REASON": "power"})
                entered = ENTERING.findall(log)
                self.assertIn(expected, entered, log[-4000:])
                if expected == "EpubReader":
                    self.assertEqual(LOADED.findall(log)[:1], [("1", "2")], log[-3000:])

    def test_7_sleep_inside_transfer_from_home_stays_home(self):
        self.run_sim("sleep-home", ["1500:RIGHT", "1900:RIGHT", "2300:RIGHT", "2700:UP", "3100:CONFIRM",
                                    "5000:SLEEP"], end=9000)
        state = json.loads((self.store / "state.json").read_text())
        self.assertFalse(state["lastSleepFromReader"], state)


if __name__ == "__main__":
    unittest.main(verbosity=2)
