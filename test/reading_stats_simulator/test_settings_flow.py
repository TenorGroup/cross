"""Kiểm việc chọn nút đánh thức được lưu, và một nhịp Quay lại trả về đúng nhóm Home.

Hợp đồng nhịp đo từ binary frozen 21/09:
- Màn Home có 5 thẻ; hai nút cạnh đổi thẻ, hai nút trước đi vòng các hàng.
- Từ Home, UP mở Cài đặt. Thẻ này có dòng "Gửi file" rồi tám nhóm theo thứ tự:
  Hiển thị, Ngủ, Trình đọc, Điều khiển, Hệ thống, Thiết bị, Bàn phím, Khác. Vì
  con trỏ bắt đầu ở "Gửi file", Ngủ cần hai nhịp RIGHT.
- Trong màn Cài đặt: hai nút cạnh đổi nhóm, hai nút trước đi hàng; hàng enum mở
  popup (không đổi tại chỗ).
- Hàng "Nút đánh thức" của nhóm Ngủ là enum bốn lựa chọn:
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

# Home -> the Cai dat (UP) -> nhom Ngu (RIGHT x2) -> Chon: mo man Cai dat.
MO_NHOM_NGU = '1000:UP;1500:RIGHT;2000:RIGHT;2500:CONFIRM;'
# Tu hang dau cua Ngu, RIGHT x6 den Nut danh thuc, Chon mo popup, RIGHT x3 toi "Tat ca", Chon ap dung.
CHON_TAT_CA = '3500:RIGHT;4000:RIGHT;4500:RIGHT;5000:RIGHT;5500:RIGHT;6000:RIGHT;6500:CONFIRM;7500:RIGHT;8100:RIGHT;8700:RIGHT;9300:CONFIRM;'


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
                CROSSPOINT_SIM_INPUT_SCRIPT=MO_NHOM_NGU + CHON_TAT_CA + '11200:BACK;13000:QUIT')
            run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env,
                                 capture_output=True, text=True, timeout=30)
            log = run.stdout + run.stderr
            artifacts = Path(os.environ.get('CROSSPOINT_TEST_ARTIFACTS', sd / 'artifacts'))
            artifacts.mkdir(parents=True, exist_ok=True)
            (artifacts / 'settings-flow-input.txt').write_text(MO_NHOM_NGU + CHON_TAT_CA + '11200:BACK;13000:QUIT')
            (artifacts / 'settings-flow.log').write_text(log)
            self.assertEqual(run.returncode, 0, log)
            self.assertEqual(json.loads((store / 'settings.json').read_text())['wakeButtons'], 3, log)
            # Mot nhom Ngu, mot lan mo man Cai dat.
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
            (artifacts / 'settings-flow-restart.log').write_text(run.stdout + run.stderr)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(json.loads((store / 'settings.json').read_text())['wakeButtons'], 3)
