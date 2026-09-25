"""v1.0.16: a jump to a TOC entry deep in a chapter not laid out yet reads the chapter's file once.

A jump lays the chapter out until the entry's anchor is on a page, a tick of about 20 ms at a time.
After every tick it also opened the chapter's section file for its anchor map (X3 r05: "Failed to
open file" between every tick, 94 pages in 6.5 s). That file is not written while the jump lays the
chapter out, so it cannot hold the anchor when it did not before the build: the ticks look in the
anchors the build has collected, and the file is read once, before the build starts.

The jump still lands on the anchor's page (the page is laid out, never guessed), and the rest of the
chapter is laid out afterwards, so the page total comes out whole.
"""

import json
import os
import re
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get("TEST_PROGRAM", REPO / ".pio/build/simulator_x3_uc8279/program"))
PARAGRAPH = "Mot doan van bia de dan trang, mua nang gio chieu sang toi mat tay long viec chu sach trang. " * 4
PARAGRAPHS = 400
DEEP_AT = 320  # the anchor sits 80 % into the chapter, as "Muc 3.4" does in the X3 test book
JUMP_AT_MS = 4000
RESOLVED = re.compile(r"^\[(\d+)\] .*Resolved anchor 'muc4' to page (\d+)", re.M)
SAVED = re.compile(r"Progress saved: spine=(\d+) offset=\d+ page=(\d+)")
OPENS = re.compile(r"^\[SIM\] open failed: .*/sections/1\.bin ", re.M)
STAMP = re.compile(r"^\[(\d+)\] ", re.M)


def write_epub(path: Path) -> None:
    """Two chapters; the contents list has the first chapter and one entry deep in the second."""
    body = "".join(
        (f'<h2 id="muc4">Muc 4</h2>' if p == DEEP_AT else "") + f"<p>{PARAGRAPH}</p>" for p in range(PARAGRAPHS))
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as epub:
        epub.writestr(zipfile.ZipInfo("mimetype"), "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        epub.writestr("META-INF/container.xml",
                      '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" '
                      'version="1.0"><rootfiles><rootfile full-path="book.opf" '
                      'media-type="application/oebps-package+xml"/></rootfiles></container>')
        head = '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>T</title></head><body>'
        # The first chapter runs several pages, so the press that starts the hold turns a page in it.
        first = "".join(f"<p>{PARAGRAPH}</p>" for _ in range(12))
        epub.writestr("c1.xhtml", f'{head}<h1 id="c1">Chuong 1</h1>{first}</body></html>')
        epub.writestr("c2.xhtml", f'{head}<h1 id="c2">Chuong 2</h1>{body}</body></html>')
        points = [("c1.xhtml#c1", "Chuong 1"), ("c2.xhtml#muc4", "Muc 4")]
        nav = "".join(f'<navPoint id="n{i}" playOrder="{i}"><navLabel><text>{label}</text></navLabel>'
                      f'<content src="{src}"/></navPoint>' for i, (src, label) in enumerate(points, 1))
        epub.writestr("toc.ncx", '<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" '
                                 f'version="2005-1"><head/><docTitle><text>Muc sau</text></docTitle>'
                                 f'<navMap>{nav}</navMap></ncx>')
        epub.writestr("book.opf",
                      '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" '
                      'unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
                      '<dc:title>Muc sau</dc:title><dc:identifier id="id">toc-deep</dc:identifier>'
                      '<dc:language>vi</dc:language></metadata><manifest>'
                      '<item id="c1" href="c1.xhtml" media-type="application/xhtml+xml"/>'
                      '<item id="c2" href="c2.xhtml" media-type="application/xhtml+xml"/>'
                      '<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest>'
                      '<spine toc="ncx"><itemref idref="c1"/><itemref idref="c2"/></spine></package>')


class TocDeepJumpTest(unittest.TestCase):
    def test_deep_jump_reads_the_section_file_once(self):
        tmp = tempfile.TemporaryDirectory(prefix="cross-toc-deep-")
        self.addCleanup(tmp.cleanup)
        sd = Path(tmp.name)
        store = sd / ".crosspoint"
        store.mkdir()
        (sd / "books").mkdir()
        write_epub(sd / "books/sau.epub")
        (store / "recent.json").write_text(json.dumps({"books": [{"path": "/books/sau.epub", "title": "Sau"}]}))
        # A held page button steps one contents entry, the path the chapter list's jump takes.
        (store / "settings.json").write_text(json.dumps({"language": "VI", "fontSize": 14, "longPressButtonBehavior": 1}))
        env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}
        env.update(SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT=f"1000:CONFIRM;{JUMP_AT_MS}:RIGHT:1000;14000:QUIT")
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-3000:])
        resolved = RESOLVED.search(log)
        self.assertIsNotNone(resolved, log[-3000:])
        landed_ms, page = int(resolved.group(1)), int(resolved.group(2))
        # The jump's own stretch: from the hold to the anchor's page. Lines of the simulator's file
        # layer carry no stamp, so they are counted up to the resolved line.
        jump = log[log.index("Entering activity: EpubReader"):resolved.start()]
        opens = len(OPENS.findall(jump))
        saved = [(int(s), int(p)) for s, p in SAVED.findall(log)]
        print(f"anchor page={page} landed_ms={landed_ms} section_opens={opens} saved={saved[-3:]}")
        self.assertGreater(page, 20, "the anchor is not deep in its chapter; the test proves nothing")
        self.assertIn((1, page), saved, "the jump did not land on the anchor's page")
        # Once, to load it; it is not there yet, so its anchor map is not asked for either.
        self.assertLessEqual(opens, 1, f"the jump opened the chapter's section file {opens} times")


if __name__ == "__main__":
    unittest.main()
