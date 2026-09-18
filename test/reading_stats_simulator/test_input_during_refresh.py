"""Nút bấm phải sống sót qua lúc panel đang chạy sóng.

Trên máy thật, task vẽ giữ RenderLock trọn một khung, kể cả lúc chờ panel xong
sóng (390 ms đo trên X3), còn bộ lọc dội cần hai mẫu cách nhau trên 5 ms. Cú bấm
nào lọt trọn giữa hai vòng chính thì không tồn tại. Hai bài dưới ép đúng tình
huống đó: CROSSPOINT_SIM_REFRESH_MS giả thời gian sóng, CROSSPOINT_SIM_STRICT_SAMPLING
bỏ cú bấm chưa kịp lấy mẫu và in "[SIM] lost press". Trước P1 (khoá chặn trong
vòng chính) hai bài này phải ĐỎ vì có cú bấm mất.
"""

import json
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get("TEST_PROGRAM", REPO / ".pio/build/simulator_x3_uc8279/program"))
FRAME = re.compile(r"\[HOME\] Frame row=(-?\d+)")
PRESS = re.compile(r"\[IN\] press t=(\d+)")
LOST = "[SIM] lost press"

REFRESH_MS = 400  # ~ sóng DU của UC8279 trên X3, giữ nguyên suốt khung vẽ
PRESS_STEP_MS = 120
HOLD_MS = 80
BOOK_COUNT = 8  # nhiều hơn mức cắt 5 mục của danh sách Gần đây


def _rows(log):
    """Ring position of every [HOME] Frame line, in order."""
    return [int(row) for row in FRAME.findall(log)]


class InputDuringRefreshTest(unittest.TestCase):
    # HỢP ĐỒNG: thẻ Gần đây của fixture này có ĐÚNG 5 dòng (thẻ "Đọc tiếp" + 4
    # sách cũ; RBS cắt danh sách ở 5 mục). Sáu nhịp phải trên vòng 5 dòng nên
    # quay vòng đúng một lần: 1 -> 2 -> 3 -> 4 -> 5 -> 1 -> 2. Không có dòng log
    # nào in số dòng, nên hằng số này nằm ở đây; đổi fixture thì phải đổi nó.
    RECENT_ROWS = 5

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="cross-input-refresh-")
        self.addCleanup(self.tmp.cleanup)
        self.sd = Path(self.tmp.name)
        self.store = self.sd / ".crosspoint"
        self.store.mkdir()
        (self.store / "settings.json").write_text(
            json.dumps({"language": "VI", "uiTheme": 4, "sleepTimeout": 10, "globalStatusBarMode": 0})
        )
        (self.store / "state.json").write_text(
            json.dumps(
                {
                    "openEpubPath": "",
                    "lastSleepFromReader": False,
                    "showBootScreen": False,
                    "readerActivityLoadCount": 0,
                }
            )
        )

    def run_sim(self, script):
        env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}
        env.update(
            SDL_VIDEODRIVER="dummy",
            CROSSPOINT_SIM_SD=str(self.sd),
            CROSSPOINT_SIM_REFRESH_MS=str(REFRESH_MS),
            CROSSPOINT_SIM_STRICT_SAMPLING="1",
            CROSSPOINT_SIM_INPUT_SCRIPT=script,
        )
        result = subprocess.run(
            [str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=60
        )
        log = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, log[-4000:])
        return log

    @staticmethod
    def press_script(at_ms, button):
        return "".join(
            f"{at_ms + i * PRESS_STEP_MS}:{button}:{HOLD_MS};" for i in range(6)
        )

    def test_home_row_walk_keeps_every_press(self):
        """Sáu nhịp phải trong lúc vẽ phải đi đúng sáu dòng, không mất nhịp nào.

        Hợp đồng fixture: thẻ Gần đây có RECENT_ROWS dòng, nên kỳ vọng là
        ((dòng đầu - 1) + 6) % RECENT_ROWS + 1 (ở đây 1 -> 2 vì quay một vòng).
        """
        books = []
        for i in range(1, BOOK_COUNT + 1):
            (self.sd / f"book{i}.txt").write_text("Mot doan van ban de test. " * 40)
            books.append(
                {
                    "path": f"/book{i}.txt",
                    "title": f"Sach {i}",
                    "author": "Tac gia",
                    "coverBmpPath": "",
                    "excerpt": "Trich doan.",
                }
            )
        (self.store / "recent.json").write_text(json.dumps({"books": books}))

        log = self.run_sim(self.press_script(2000, "RIGHT") + "4000:QUIT")
        self.assertEqual(log.count(LOST), 0, log[-4000:])
        # Dòng đầu trước cú bấm đầu, và dòng cuối: firmware P1 in "[IN] press
        # t=..." cho mọi cú bấm được lấy mẫu, nên chỗ cắt là mốc chắc chắn.
        truoc, sau = log.split("[IN] press", 1)
        rows_truoc = _rows(truoc)
        rows_sau = _rows(sau)
        self.assertTrue(rows_truoc and rows_sau, log[-4000:])
        dau, cuoi = rows_truoc[-1], rows_sau[-1]
        # Sáu nhịp trên vòng RECENT_ROWS dòng: quay vòng khi chạm mép.
        self.assertEqual(cuoi, (dau - 1 + 6) % self.RECENT_ROWS + 1, log[-4000:])
        # Sáu cú bấm của kịch bản, mỗi cú đúng một dòng (không cú nào bị bỏ).
        self.assertEqual(len(PRESS.findall(log)), 6, log[-4000:])
        # Sáu nhịp trong lúc panel chạy sóng phải gộp lại: ít khung hơn số nhịp.
        self.assertLess(len(rows_sau), 6, log[-4000:])
        # So do tho de log XANH tu mang bang chung, khong chi chu "OK".
        print(
            f"HOME do: row {dau} -> {cuoi} tren vong {self.RECENT_ROWS} dong, "
            f"khung sau nhip dau = {len(rows_sau)}, so nhip lay mau = {len(PRESS.findall(log))}, "
            f"lost press = {log.count(LOST)}"
        )

    def test_settings_tab_step_keeps_every_press(self):
        """Nút cạnh xuống trên màn Cài đặt: nhịp đổi nhóm không được mất."""
        # Vào Cài đặt bằng đường có sẵn của test_settings_flow: Home -DOWN x4->
        # thẻ Cài đặt -RIGHT x4- nhóm Hệ thống -Confirm- mở màn Cài đặt.
        vao_cai_dat = "1000:DOWN;1500:DOWN;2000:DOWN;2500:DOWN;" "3000:RIGHT;3300:RIGHT;3600:RIGHT;3900:RIGHT;4300:CONFIRM;"
        log = self.run_sim(vao_cai_dat + self.press_script(6000, "DOWN") + "8000:QUIT")
        self.assertEqual(log.count("Entering activity: Settings"), 1, log[-4000:])
        self.assertEqual(log.count(LOST), 0, log[-4000:])
        # Sáu nhịp cuối là sáu lần bấm nút cạnh, tất cả phải được lấy mẫu.
        self.assertEqual(len([t for t in PRESS.findall(log) if int(t) >= 6000]), 6, log[-4000:])
        print(
            f"CAI DAT do: nhom sau 6000ms lay mau = "
            f"{len([t for t in PRESS.findall(log) if int(t) >= 6000])}, "
            f"lost press = {log.count(LOST)}"
        )


if __name__ == "__main__":
    unittest.main()
