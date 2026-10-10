"""X4 Pro, man may chu da noi: dia chi dai ngat dong o ranh gioi hop ly (sau "/" hoac "-", truoc "."), khong de mot dong chi co
dau cau ("/" le): moi dong tiep theo cua dia chi co be rong muc >= 40 px (khoang 3 ky tu chu so tro len).
Do tren anh 1 bit: cac dong chu duoi ma QR trong khung dau tien.
Chay: GUIFILE_X4PRO_PROGRAM=... python3 test/gui_file_overlap/test_g5_address_break_x4pro.py"""
import itertools
import sys
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from measure import segment
from plan import plan, program, run_case

_port = itertools.count(30800)
MIN_INK = 40


def one(job):
    tier, lang, long, state, script, shots, env, saved = job
    imgs, _ = run_case('x4pro', tier, lang, 1, long, state, script, shots, env, saved, next(_port))
    bad = []
    for name, im in imgs.items():
        blocks, _m = segment(im, 'x4pro')
        qr = [b for b in blocks if b.kind == 'qr']
        rings = [b for b in blocks if b.kind == 'ring' and b.w > 300 and b.y0 < 120]
        if not qr or not rings:
            bad.append(f'khong thay QR hoac khung: {state}-{name}')
            continue
        below = [b for b in blocks if b.kind == 'text' and b.y0 >= qr[0].y1 and b.y1 <= rings[0].y1]
        for b in below:
            if b.w < MIN_INK:
                bad.append(f'dong dia chi qua ngan ({b.w} px, khong du 2 ky tu chu so): {state}-{name} co chu {tier} {lang} {b.box()}')
    return bad


class AddressBreakX4Pro(unittest.TestCase):
    def test_no_lone_punctuation_line(self):
        self.assertTrue(program('x4pro').exists(), 'chua dung gia lap: pio run -e simulator_x4pro')
        jobs = []
        for tier, lang in itertools.product((0, 1, 2), ('VI', 'EN')):
            for state, script, shots, env, saved in plan('x4pro', tier, 1):
                if state == 'sta-server':
                    jobs.append((tier, lang, 1, state, script, shots, env, saved))
        with ThreadPoolExecutor(6) as ex:
            bad = [m for found in ex.map(one, jobs) for m in found]
        for line in bad:
            print('TRUOT', line)
        self.assertEqual(bad, [])


if __name__ == '__main__':
    unittest.main()
