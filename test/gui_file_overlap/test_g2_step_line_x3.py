"""X3, man may chu: dong chi buoc ("Ket noi Wi-Fi" o buoc 1 cua diem phat, "Mo dia chi" o buoc 2 va o may chu da noi) la
THONG TIN: hien ca khi tat meo, nam ngay duoi ma QR trong khoi noi dung. Anh bat meo va tat meo phai giong het nhau.
Chay: GUIFILE_X3_PROGRAM=... python3 test/gui_file_overlap/test_g2_step_line_x3.py"""
import itertools
import sys
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
from measure import find_qr, QR_PX
from plan import plan, program, run_case

_port = itertools.count(30500)


def run(tier, lang, tips, long):
    out = {}
    for state, script, shots, env, saved in plan('x3', tier, long):
        if state not in ('sta-server', 'ap'):
            continue
        imgs, _ = run_case('x3', tier, lang, tips, long, state, script, shots, env, saved, next(_port))
        for name, im in imgs.items():
            out[(state, name)] = np.array(im.convert('L')) < 128
    return out


class StepLineX3(unittest.TestCase):
    def test_step_is_information(self):
        self.assertTrue(program('x3').exists(), 'chua dung gia lap: pio run -e simulator_x3_uc8279')
        combos = list(itertools.product((0, 1, 2), ('VI', 'EN'), (0, 1), (0, 1)))
        with ThreadPoolExecutor(6) as ex:
            res = dict(zip(combos, ex.map(lambda c: run(*c), combos)))
        bad = []
        for tier, lang, tips, long in combos:
            if tips:
                continue
            off, on = res[(tier, lang, 0, long)], res[(tier, lang, 1, long)]
            for key, M in off.items():
                if (M[:-32] != on[key][:-32]).any():  # tru dai nut (dong ho doi theo gio chay)
                    bad.append(f'bat meo va tat meo khac nhau: {key} co chu {tier} {lang} ten {"dai" if long else "ngan"}')
                qr = find_qr(M)
                if len(qr) != 1:
                    bad.append(f'khong thay QR: {key} co chu {tier} {lang}')
                    continue
                x0, y0 = qr[0]
                step = M[y0 + QR_PX + 2:y0 + QR_PX + 40, 60:470]
                if step.sum() < 60:
                    bad.append(f'khong co dong chi buoc duoi QR: {key} che do meo tat, co chu {tier} {lang} ten {"dai" if long else "ngan"}')
        for line in bad:
            print('TRUOT', line)
        self.assertEqual(bad, [])


if __name__ == '__main__':
    unittest.main()
