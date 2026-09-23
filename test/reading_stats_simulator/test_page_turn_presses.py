"""Mỗi cú bấm lật trang phải thành đúng một lượt lật, đúng thứ tự.

Máy thật: task vẽ giữ RenderLock suốt lượt sóng (khoảng 400 ms trên X3), trình
đọc có khoá lật 200 ms sau mỗi lượt, và vòng chính chỉ lấy mẫu nút mỗi lượt
chạy. CROSSPOINT_SIM_REFRESH_MS giả thời gian sóng, CROSSPOINT_SIM_STRICT_SAMPLING
bỏ cú bấm mà vòng chính không kịp lấy mẫu (in "[SIM] lost press"). Các nhịp bấm
dưới đây lấy từ tay người: đọc lướt, bấm dồn, chạm nhẹ, bấm sau khi máy nằm im.

Bằng chứng lật trang: dòng `Progress saved: ... page=N` sau mỗi lần vẽ. Sách
mẫu có một chương dài, nên N sau K cú bấm tới phải đúng bằng K.
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
REFRESH_MS = 400  # ~ sóng của UC8279 trên X3, giữ trọn khung vẽ
OPEN_AT_MS = 1000
FIRST_PRESS_MS = 3500
LOST = "[SIM] lost press"
SAVED = re.compile(r"Progress saved: spine=(\d+) offset=\d+ page=(\d+)")
PRESS = re.compile(r"\[IN\] press t=(\d+)")

DOAN = ("Mot doan van mau cho bai lat trang, du dai de moi trang day chu. " * 6 + "\n") * 10


def write_epub(path: Path) -> None:
    """Một chương rất dài: 60 cú bấm vẫn chưa chạm cuối chương."""
    with zipfile.ZipFile(path, "w") as epub:
        epub.writestr("mimetype", "application/epub+zip")
        epub.writestr(
            "META-INF/container.xml",
            '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" '
            'version="1.0"><rootfiles><rootfile full-path="book.opf" '
            'media-type="application/oebps-package+xml"/></rootfiles></container>',
        )
        epub.writestr(
            "book.opf",
            '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" '
            'unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
            "<dc:title>Page turn fixture</dc:title><dc:identifier id=\"id\">page-turn</dc:identifier>"
            "<dc:language>vi</dc:language></metadata>"
            '<manifest><item id="c1" href="c1.xhtml" media-type="application/xhtml+xml"/></manifest>'
            '<spine><itemref idref="c1"/></spine></package>',
        )
        epub.writestr(
            "c1.xhtml",
            '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head>'
            "<title>Chuong</title></head><body>" + "".join(f"<p>{DOAN}</p>" for _ in range(40)) + "</body></html>",
        )


def presses(button, hold_ms, count, gap_ms, start_ms=FIRST_PRESS_MS):
    """Cú bấm theo nhịp tay: nghỉ gap_ms, giữ hold_ms, lặp lại."""
    period = hold_ms + gap_ms
    return "".join(f"{start_ms + i * period}:{button}:{hold_ms};" for i in range(count))


class PageTurnPressesTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="cross-page-turn-")
        self.addCleanup(self.tmp.cleanup)
        self.sd = Path(self.tmp.name)
        store = self.sd / ".crosspoint"
        store.mkdir()
        (self.sd / "books").mkdir()
        write_epub(self.sd / "books/lat.epub")
        (store / "recent.json").write_text(json.dumps({"books": [{"path": "/books/lat.epub", "title": "Lat"}]}))
        (store / "settings.json").write_text(json.dumps({"language": "VI", "fontSize": 14}))

    def run_sim(self, script, end_ms):
        env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}
        env.update(
            SDL_VIDEODRIVER="dummy",
            CROSSPOINT_SIM_SD=str(self.sd),
            CROSSPOINT_SIM_REFRESH_MS=str(REFRESH_MS),
            CROSSPOINT_SIM_STRICT_SAMPLING="1",
            CROSSPOINT_SIM_INPUT_SCRIPT=f"{OPEN_AT_MS}:CONFIRM;{script}{end_ms}:QUIT",
        )
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-4000:])
        self.assertIn("Entering activity: EpubReader", log)
        return log

    def check(self, name, script, end_ms, expected_page, press_count):
        log = self.run_sim(script, end_ms)
        pages = [int(p) for s, p in SAVED.findall(log) if s == "0"]
        sampled = [int(t) for t in PRESS.findall(log) if int(t) >= FIRST_PRESS_MS - 50]
        lost = log.count(LOST)
        last = pages[-1] if pages else None
        print(f"{name}: bam={press_count} lay_mau={len(sampled)} sim_lost={lost} trang_cuoi={last} "
              f"can={expected_page} so_lan_luu={len(pages)}")
        self.assertEqual(lost, 0, f"{name}: vong chinh khong lay mau kip\n" + log[-3000:])
        self.assertEqual(last, expected_page, f"{name}: trang {pages}")
        # Every saved position moves forward one page at a time or jumps ahead;
        # a backward step inside a run of forward presses means reordering.
        self.assertEqual(pages, sorted(pages), f"{name}: trang di lui {pages}")

    def test_nhip_doc_luot(self):
        """80 ms, nghỉ 700 ms: mỗi cú bấm một trang."""
        n = 10
        self.check("doc_luot", presses("DOWN", 80, n, 700), FIRST_PRESS_MS + n * 780 + 2500, n, n)

    def test_bam_don_trong_luc_ve(self):
        """60 ms, nghỉ 250 ms: cú bấm rơi giữa lượt sóng 400 ms, không được gộp mất."""
        n = 10
        self.check("bam_don", presses("DOWN", 60, n, 250), FIRST_PRESS_MS + n * 310 + 6000, n, n)

    def test_cham_nhe(self):
        """30 ms, nghỉ 700 ms: chạm nhẹ vẫn là một trang."""
        n = 10
        self.check("cham_nhe", presses("DOWN", 30, n, 700), FIRST_PRESS_MS + n * 730 + 2500, n, n)

    def test_nut_mat_truoc_bam_don(self):
        """Nút phải mặt trước, bấm dồn 60/250."""
        n = 8
        self.check("mat_truoc", presses("RIGHT", 60, n, 250), FIRST_PRESS_MS + n * 310 + 6000, n, n)

    def test_bam_sau_khi_nam_im(self):
        """Mỗi cú bấm sau 4,5 s nằm im: vòng chờ rảnh 50 ms không được nuốt nó."""
        n = 4
        self.check("sau_nam_im", presses("DOWN", 80, n, 4500), FIRST_PRESS_MS + n * 4580 + 2000, n, n)

    def test_doi_huong_giua_luc_ve(self):
        """Ba cú tới rồi một cú lui, sát nhau: kết quả phải là trang 2, đúng thứ tự."""
        script = presses("DOWN", 60, 3, 150) + f"{FIRST_PRESS_MS + 3 * 210}:UP:60;"
        log = self.run_sim(script, FIRST_PRESS_MS + 6000)
        pages = [int(p) for s, p in SAVED.findall(log) if s == "0"]
        print(f"doi_huong: trang={pages} sim_lost={log.count(LOST)}")
        self.assertEqual(log.count(LOST), 0, log[-3000:])
        self.assertEqual(pages[-1] if pages else None, 2, f"doi_huong: trang {pages}")


if __name__ == "__main__":
    unittest.main()
