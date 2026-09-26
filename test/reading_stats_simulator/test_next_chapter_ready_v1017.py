"""v1.0.17: the next chapter is laid out while the reader sits on the last page of this one.

Turning into a chapter not laid out yet started its build inside the paint: on the X3 (r39) the
turn took 1.6 s to a readable page, 724 ms of it the build. A quiet pass on the last page of a
whole chapter now lays out the first pages of the next one into a partial section file, so the
turn loads its page like any cached one. The pages are the pages a build inside the turn makes:
the chapter read after the early layout is compared, page by page, with one turned into at once.
"""

import json
import os
import re
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

from PIL import Image, ImageChops

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get("TEST_PROGRAM", REPO / ".pio/build/simulator_x3_uc8279/program"))
PRESS = re.compile(r"\[(\d+)\] \[INF\] \[IN\] press t=\d+")
SAVED = re.compile(r"\[(\d+)\] \[DBG\] \[ERS\] Progress saved: spine=1 offset=0 page=0")
TEXT = "Chuong sau dai du de sinh nhieu trang khi lat qua. " * 8
SHORT = "Chuong dau chi co mot trang."


def write_epub(path):
    bodies = [f"<p>{SHORT}</p>", "".join(f"<p>{TEXT}</p>" for _ in range(40)), f"<p>{SHORT}</p>"]
    nav = "".join(f'<navPoint id="n{i}" playOrder="{i}"><navLabel><text>Chuong {i}</text></navLabel>'
                  f'<content src="c{i}.xhtml"/></navPoint>' for i in range(1, 4))
    with zipfile.ZipFile(path, "w") as epub:
        epub.writestr("mimetype", "application/epub+zip")
        epub.writestr("META-INF/container.xml",
                      '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" '
                      'version="1.0"><rootfiles><rootfile full-path="book.opf" '
                      'media-type="application/oebps-package+xml"/></rootfiles></container>')
        epub.writestr("book.opf",
                      '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" '
                      'unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
                      '<dc:title>Next chapter</dc:title><dc:identifier id="id">next-chapter</dc:identifier>'
                      '<dc:language>vi</dc:language></metadata><manifest>' +
                      "".join(f'<item id="c{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>'
                              for i in range(1, 4)) +
                      '<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest>'
                      '<spine toc="ncx">' + "".join(f'<itemref idref="c{i}"/>' for i in range(1, 4)) +
                      "</spine></package>")
        epub.writestr("toc.ncx",
                      '<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">'
                      f"<head/><docTitle><text>Next chapter</text></docTitle><navMap>{nav}</navMap></ncx>")
        for i, body in enumerate(bodies, start=1):
            epub.writestr(f"c{i}.xhtml",
                          '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head>'
                          f"<title>Chuong {i}</title></head><body>{body}</body></html>")


class NextChapterReadyTest(unittest.TestCase):
    def run_book(self, first_turn_ms):
        temp = tempfile.TemporaryDirectory(prefix="cross-next-chapter-")
        self.addCleanup(temp.cleanup)
        sd = Path(temp.name)
        store = sd / ".crosspoint"
        store.mkdir()
        (sd / "books").mkdir()
        write_epub(sd / "books/sach.epub")
        (store / "recent.json").write_text(json.dumps({"books": [{"path": "/books/sach.epub", "title": "Sach"}]}))
        (store / "settings.json").write_text(json.dumps({"language": "VI", "fontSize": 14}))
        # Chapter one is one page: the book opens on its last page. Three turns into chapter two.
        turns = [first_turn_ms, first_turn_ms + 2500, first_turn_ms + 5000]
        script = "1000:CONFIRM;" + "".join(f"{ms}:RIGHT;" for ms in turns) + f"{turns[-1] + 2500}:QUIT"
        shots = ";".join(f"{ms + 2000}:{sd}/page{n}.bmp" for n, ms in enumerate(turns))
        env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}
        env.update(SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT=script,
                   CROSSPOINT_SIM_SCREENSHOTS=shots)
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=90)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-3000:])
        pages = [Image.open(sd / f"page{n}.bmp").convert("L") for n in range(3)]
        return log, pages

    @staticmethod
    def turn_into_chapter_two(log):
        """Log lines of the turn into chapter two, from its press to its page's progress write, and
        the simulator milliseconds between the two."""
        saved = SAVED.search(log)
        press = list(PRESS.finditer(log, 0, saved.start()))[-1]
        return log[press.start():saved.end()], int(saved.group(1)) - int(press.group(1))

    def test_next_chapter_is_laid_out_on_the_last_page(self):
        # Four seconds on the last page of chapter one, then the turn.
        log, pages = self.run_book(5000)
        turn, turn_ms = self.turn_into_chapter_two(log)
        # At once: the turn comes before the reader has sat on the page.
        control, control_pages = self.run_book(1500)
        control_turn, control_ms = self.turn_into_chapter_two(control)
        print(f"simulator: press to chapter two's page {turn_ms} ms laid out early, {control_ms} ms laid out "
              "in the turn", flush=True)
        self.assertIn("Cache not found, building", control_turn, "the control turn did not lay out the chapter")
        self.assertNotIn("Cache not found, building", turn, "the turn into the next chapter laid it out itself")
        self.assertNotIn("Streamed temp HTML", turn, "the turn into the next chapter unpacked it itself")
        self.assertNotIn("sections/1.bin", turn, "the turn looked for a section file that was not there")
        for n, (early, late) in enumerate(zip(pages, control_pages)):
            width, height = early.size
            box = (0, 0, width, height - 60)  # the status bar carries the clock
            self.assertIsNone(ImageChops.difference(early.crop(box), late.crop(box)).getbbox(),
                              f"page {n} of chapter two differs from the one laid out in the turn")


if __name__ == "__main__":
    unittest.main()
