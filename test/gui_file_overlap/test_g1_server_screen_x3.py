"""X3, man may chu cua luong Gui file (da noi va diem phat):
(a) dai nut (ca man Calibre) dung dung nhan cua cac man khac cho Back (ky hieu o che do Ky hieu, "« Back" o che do Chu), khong con "« Thoat";
(b) chu, ma QR (va dong chi buoc) lien nhau thanh mot khoi, khoi can giua doc trong vung noi dung, cach thanh >= 8 px.
Chay: GUIFILE_X3_PROGRAM=... python3 test/gui_file_overlap/test_g1_server_screen_x3.py"""
import itertools
import sys
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
from hint_audit import bar_of
from plan import plan, program, run_case

_port = itertools.count(30400)
SERVER_SHOTS = (('sta-server', 'p1'), ('ap', 'step1'), ('ap', 'step2'), ('calibre', 'calibre'))


def crop(im, y0, y1, x0, x1):
    return (np.array(im.convert('L'))[y0:y1, x0:x1] < 128)


def run(mode, tier, lang):
    """Mot cau hinh: man chooser (nhan Back chuan) va cac man may chu, tat mep."""
    extra = {'tenorButtonSymbols': mode}
    out = {}
    for state, script, shots, env, saved in plan('x3', tier, 0):
        if state not in ('chooser', 'sta-server', 'ap', 'calibre'):
            continue
        imgs, log = run_case('x3', tier, lang, 0, 0, state, script, shots, env, saved, next(_port), extra=extra)
        for name, im in imgs.items():
            out[(state, name)] = (im, bar_of(log))
    return out


class ServerScreenX3(unittest.TestCase):
    def test_back_label_and_centered_block(self):
        self.assertTrue(program('x3').exists(), 'chua dung gia lap: pio run -e simulator_x3_uc8279')
        combos = list(itertools.product((0, 1), (0, 1, 2), ('VI', 'EN')))
        with ThreadPoolExecutor(6) as ex:
            results = list(ex.map(lambda c: (c, run(*c)), combos))
        bad = []
        for (mode, tier, lang), shots in results:
            chooser, _ = shots[('chooser', 'chooser')]
            H = chooser.height
            # o nhan 1 (sau so pin): x 84..165, hang cuoi 30 px
            ref = crop(chooser, H - 30, H - 3, 84, 165)
            for key in SERVER_SHOTS:
                im, bar = shots[key]
                same = (crop(im, H - 30, H - 3, 84, 165) == ref).all()
                if not same:
                    bad.append(f'nhan Back khac cac man khac: {key} che do {mode} co chu {tier} {lang}')
                if key[0] == 'calibre':
                    continue  # man Calibre: chi kiem nhan Back, khoi chu khong co QR
                # khoi noi dung: muc o tren thanh path
                M = np.array(im.convert('L')) < 128
                _, by, _, _ = bar
                rows = np.where(M[:by - 2].any(1))[0]
                top, bottom = rows.min(), rows.max() + 1
                above, below = top - 16, (by - 8) - bottom
                if below < 0 or abs(above - below) > 8:
                    bad.append(f'khoi khong can giua doc: {key} che do {mode} co chu {tier} {lang}: tren {above}, duoi {below}')
        for line in bad:
            print('TRUOT', line)
        self.assertEqual(bad, [])


if __name__ == '__main__':
    unittest.main()
