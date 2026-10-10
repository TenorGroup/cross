"""X4 Pro, man may chu da noi, diem phat va Calibre: dau man chi co hang trang thai (gio, bieu tuong, pin); khong con hang ten
mang va vach ngang. Ten mang (va dia chi IP o Calibre) nam trong khung noi dung dau tien, cat bang dau "..." neu dai.
Chay: GUIFILE_X4PRO_PROGRAM=... python3 test/gui_file_overlap/test_g4_top_of_screen_x4pro.py"""
import itertools
import sys
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
from measure import segment
from plan import plan, program, run_case

_port = itertools.count(30700)


def one(job):
    tier, lang, long, state, script, shots, env, saved = job
    imgs, _ = run_case('x4pro', tier, lang, 1, long, state, script, shots, env, saved, next(_port))
    bad = []
    for name, im in imgs.items():
        tag = f'{state}-{name} co chu {tier} {lang} ten {"dai" if long else "ngan"}'
        blocks, M = segment(im, 'x4pro')
        for b in blocks:
            if b.kind == 'rule' and 30 <= b.y0 <= 130:
                bad.append(f'con vach ngang toan man o y {b.y0}: {tag}')
        if M[33:38].any():
            bad.append(f'co muc giua hang trang thai va noi dung (y 33..37): {tag}')
        if state == 'sta-server':
            ring = min((b for b in blocks if b.kind == 'ring' and b.y0 < 120 and b.w > 300), key=lambda b: b.y0, default=None)
            qr = [b for b in blocks if b.kind == 'qr']
            if ring is None or not qr:
                bad.append(f'khong thay khung dau tien hoac QR: {tag}')
            else:
                lines = [b for b in blocks if b.kind == 'text' and ring.y0 < b.y0 and b.y1 < qr[0].y0]
                if len(lines) < 3:
                    bad.append(f'khung dau thieu dong ten mang (co {len(lines)} dong tren QR, can >= 3): {tag}')
    return bad


class TopOfScreenX4Pro(unittest.TestCase):
    def test_status_strip_only(self):
        self.assertTrue(program('x4pro').exists(), 'chua dung gia lap: pio run -e simulator_x4pro')
        jobs = []
        for tier, lang, long in itertools.product((0, 1, 2), ('VI', 'EN'), (0, 1)):
            for state, script, shots, env, saved in plan('x4pro', tier, long):
                if state in ('sta-server', 'ap', 'calibre'):
                    jobs.append((tier, lang, long, state, script, shots, env, saved))
        with ThreadPoolExecutor(6) as ex:
            bad = [m for found in ex.map(one, jobs) for m in found]
        for line in bad:
            print('TRUOT', line)
        self.assertEqual(bad, [])


if __name__ == '__main__':
    unittest.main()
