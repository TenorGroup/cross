"""Kiểm việc chọn nút đánh thức được lưu, và một nhịp Quay lại trả về đúng nhóm Home.

Hợp đồng nhịp đã đo lại 16/09 (xem checkpoint-u1-wip.md và test_rework_ui.py):
- Màn Home có 5 thẻ; hai nút cạnh đổi thẻ, hai nút trước đi vòng các hàng.
- Thẻ Cài đặt là thẻ thứ năm (bốn nhịp DOWN từ thẻ đầu); trên thẻ Cài đặt, vòng
  hàng gồm bảy nhóm: Hiển thị, Trình đọc, Điều khiển, Hệ thống, Thiết bị, Bàn
  phím, Khác. "Gửi file" là hành động ở vòng 0 nên nhịp Chọn khi chưa đi vòng sẽ
  mở luồng truyền tệp - bản cũ của bài này dựa vào chỗ đó nên đã sai hợp đồng.
- Trong màn Cài đặt: hai nút cạnh đổi nhóm, hai nút trước đi hàng; hàng enum mở
  popup (không đổi tại chỗ).
- Hàng "Nút đánh thức" là hàng thứ hai của nhóm Hệ thống, enum bốn lựa chọn:
  0 Nguồn, 1 Phải, 2 Cạnh, 3 Tất cả. Bản này chọn mức 3.
"""

import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))

# Home -> the Cai dat (DOWN x4) -> nhom He thong (RIGHT x4) -> Chon: mo man Cai dat.
MO_NHOM_HE_THONG = '1000:DOWN;1500:DOWN;2000:DOWN;2500:DOWN;3000:RIGHT;3300:RIGHT;3600:RIGHT;3900:RIGHT;4300:CONFIRM;'
# Hang 1 = Nut danh thuc: Chon de vao hang, Chon de mo popup, RIGHT x3 toi "Tat ca", Chon ap dung.
CHON_TAT_CA = '5500:RIGHT;6000:CONFIRM;7200:RIGHT;7800:RIGHT;8400:RIGHT;9000:CONFIRM;'


class SettingsFlowTest(unittest.TestCase):
    def test_wake_choice_persists_and_back_keeps_home_group(self):
        with tempfile.TemporaryDirectory(prefix='cross-settings-flow-') as tmp:
            sd = Path(tmp)
            store = sd / '.crosspoint'
            store.mkdir()
            (store / 'settings.json').write_text(json.dumps({'language': 'VI', 'wakeButtons': 0}))
            env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
            env.update(
                SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=tmp,
                CROSSPOINT_SIM_INPUT_SCRIPT=MO_NHOM_HE_THONG + CHON_TAT_CA + '11200:BACK;13000:QUIT')
            run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env,
                                 capture_output=True, text=True, timeout=30)
            log = run.stdout + run.stderr
            self.assertEqual(run.returncode, 0, log)
            self.assertEqual(json.loads((store / 'settings.json').read_text())['wakeButtons'], 3, log)
            # Mot nhom He thong, mot lan mo man Cai dat.
            self.assertEqual(log.count('Entering activity: Settings'), 1, log)
            self.assertEqual(log.count('Entering activity: CrossPointWebServer'), 0, log)
            # Mot nhip Quay lai phai ROI man Cai dat va tra ve Home. Do 16/09: Home khong
            # phat lai "Entering activity: Home" khi duoc hien lai (no chua he bi huy),
            # nen bang chung dich la: man Cai dat ra khoi ngan xep (size = 0) va sau do
            # khong mot man nao khac duoc mo. Neu Quay lai con o Cai dat thi khong co
            # dong thoat nao; neu di lac sang man khac thi co dong Entering khac.
            self.assertEqual(log.count('Exiting activity: Settings'), 1, log)
            self.assertEqual(log.count('Popped from activity stack, new size = 0'), 1, log)
            sau = log.split('Exiting activity: Settings', 1)[1]
            con_lai = [l.strip() for l in sau.splitlines() if 'Entering activity' in l or 'Exiting activity' in l]
            self.assertEqual(con_lai, [], f'mot nhip Quay lai phai ve thang Home: {con_lai[:4]}')

            # Khoi dong lai: lua chon da luu phai con nguyen.
            env['CROSSPOINT_SIM_INPUT_SCRIPT'] = '1600:QUIT'
            run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env,
                                 capture_output=True, text=True, timeout=10)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(json.loads((store / 'settings.json').read_text())['wakeButtons'], 3)
