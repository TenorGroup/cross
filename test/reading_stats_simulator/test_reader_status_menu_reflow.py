"""Reader status-bar mode changes must reflow when the menu closes."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile

import numpy as np
from PIL import Image


REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get("STATUSBAR_PROGRAM", REPO / ".pio/build/simulator_x3_uc8279/program"))
FONT = Path(
    os.environ.get(
        "STATUSBAR_FONT",
        "/Users/tuan/Tenor/outputs/260917-v103-luna-takeover/statusbar-paintorder/BeVietnamPro_18.cpfont",
    )
)

CONTAINER = """<?xml version="1.0" encoding="utf-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
<rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>"""

OPF = """<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id">
<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
<dc:title>Menu reflow fixture</dc:title><dc:identifier id="id">menu-reflow</dc:identifier>
<dc:language>en</dc:language></metadata>
<manifest><item id="c1" href="c1.xhtml" media-type="application/xhtml+xml"/>
<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest>
<spine toc="ncx"><itemref idref="c1"/></spine></package>"""

NCX = """<?xml version="1.0" encoding="utf-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
<head><meta name="dtb:uid" content="menu-reflow"/></head>
<docTitle><text>Menu reflow fixture</text></docTitle>
<navMap><navPoint id="n1" playOrder="1"><navLabel><text>Reflow Chapter</text></navLabel>
<content src="c1.xhtml"/></navPoint></navMap></ncx>"""

CHAPTER = """<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en">
<head><title>Reflow Chapter</title></head><body>{body}</body></html>"""


def body_text() -> str:
    paragraphs = [
        "<p>U2 EPUB anchor line one. Fixed status fixture content.</p>",
        "<p>U2 EPUB anchor line two. Fixed status fixture content.</p>",
        "<p>U2 EPUB anchor line three. Fixed status fixture content.</p>",
    ]
    paragraphs.extend(
        f"<p>EPUB body paragraph {index:02d}. Stable words make the page fill deterministic for the six modes.</p>"
        for index in range(1, 80)
    )
    return "<h1>U2 EPUB anchor</h1>" + "".join(paragraphs)


def menu_script() -> str:
    return (
        "1000:CONFIRM;3000:CONFIRM;3600:DOWN;4200:DOWN;5000:RIGHT;5400:RIGHT;"
        "6000:CONFIRM;7600:LEFT;8100:LEFT;9800:CONFIRM;11200:BACK;12800:QUIT"
    )


def body_bands(path: Path) -> list[list[int]]:
    image = Image.open(path).convert("L")
    if image.width > image.height:
        image = image.rotate(90, expand=True)
    mask = np.asarray(image) < 128
    footer_y = mask.shape[0] - 32
    rows = mask[:footer_y].any(axis=1)
    bands: list[list[int]] = []
    for y in np.flatnonzero(rows).tolist():
        if not bands or y > bands[-1][1] + 1:
            bands.append([int(y), int(y)])
        else:
            bands[-1][1] = int(y)
    return [band for band in bands if band[1] >= 8]


class ReaderStatusMenuReflowTest(unittest.TestCase):
    def test_off_reflows_on_menu_close_and_keeps_anchor(self):
        with tempfile.TemporaryDirectory(prefix="cross-reader-menu-reflow-") as directory:
            root = Path(directory)
            store = root / ".crosspoint"
            store.mkdir()
            if FONT.exists():
                target = root / ".fonts" / "BeVietnamPro" / "BeVietnamPro_18.cpfont"
                target.parent.mkdir(parents=True)
                shutil.copy2(FONT, target)
            with zipfile.ZipFile(root / "audit.epub", "w") as archive:
                archive.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
                archive.writestr("META-INF/container.xml", CONTAINER)
                archive.writestr("OEBPS/content.opf", OPF)
                archive.writestr("OEBPS/toc.ncx", NCX)
                archive.writestr("OEBPS/c1.xhtml", CHAPTER.format(body=body_text()))
            settings = {
                "language": "EN",
                "uiTheme": 4,
                "sdFontFamilyName": "BeVietnamPro" if FONT.exists() else "",
                "fontSize": 18,
                "readerInkWeight": 0,
                "screenMargin": 5,
                "lineSpacing": 0,
                "letterSpacing": 0,
                "wordSpacing": 0,
                "extraParagraphSpacing": 0,
                "paragraphAlignment": 1,
                "paragraphIndent": 0,
                "paragraphIndentVersion": 1,
                "textSpacingVersion": 3,
                "dropCapMode": 0,
                "textAntiAliasing": 0,
                "globalStatusBarMode": 0,
                "readerStatusBarMode": 2,
                "statusBarClock": 1,
                "clockFormat": 0,
                "clockUtcOffsetQ": 76,
                "clockHasBeenSynced": 1,
            }
            (store / "settings.json").write_text(json.dumps(settings) + "\n", encoding="utf-8")
            (store / "state.json").write_text(
                json.dumps(
                    {
                        "openEpubPath": "/audit.epub",
                        "lastSleepFromReader": False,
                        "showBootScreen": False,
                        "readerActivityLoadCount": 0,
                    }
                )
                + "\n",
                encoding="utf-8",
            )
            (store / "recent.json").write_text(
                json.dumps({"books": [{"path": "/audit.epub", "title": "Menu reflow fixture"}]}) + "\n",
                encoding="utf-8",
            )
            before = root / "before.bmp"
            after = root / "after.bmp"
            env = {key: value for key, value in os.environ.items() if not key.startswith("CROSSPOINT_SIM_")}
            env.update(
                SDL_VIDEODRIVER="dummy",
                CROSSPOINT_SIM_SD=str(root),
                CROSSPOINT_SIM_INPUT_SCRIPT=menu_script(),
                CROSSPOINT_SIM_SCREENSHOTS=f"2400:{before};11800:{after}",
            )
            run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=45)
            log = run.stdout + run.stderr
            self.assertEqual(run.returncode, 0, log)
            self.assertIn("Entering activity: EpubReaderMenu", log)
            self.assertTrue(before.exists(), log)
            self.assertTrue(after.exists(), log)
            before_bands = body_bands(before)
            after_bands = body_bands(after)
            self.assertTrue(before_bands, "before image has no reader body")
            self.assertTrue(after_bands, "after image has no reader body")
            self.assertLessEqual(abs(after_bands[0][0] - before_bands[0][0]), 2)
            self.assertGreater(
                len(after_bands), len(before_bands),
                f"Off must reclaim a body band on menu close: before={before_bands} after={after_bands}",
            )
            self.assertGreaterEqual(after_bands[-1][1], before_bands[-1][1] + 20)
            stored = json.loads((store / "settings.json").read_text(encoding="utf-8"))
            self.assertEqual(stored.get("readerStatusBarMode"), 0, stored)


if __name__ == "__main__":
    unittest.main()
