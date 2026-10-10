"""Nhan dai nut X3 (che do Ky hieu va che do Chu) o 3 co chu, VI va EN: nhan khong dinh so pin, dong ho hay thanh path.
Cac man: Home, Cai dat (nhom Doc), ban phim ten may, danh sach File, va luong Gui file.
Chay: pio run -e simulator_x3_uc8279, roi python3 test/gui_file_overlap/test_hint_labels.py (GUIFILE_X3_PROGRAM chon ban dung)."""
import itertools
import os
import sys
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from hint_audit import audit, bar_of
from plan import plan, program, run_case

_port = itertools.count(30200)


def seq(keys, t0=1400, step=1000):
    return ';'.join(f'{t0 + i * step}:{k}' for i, k in enumerate(keys)), t0 + len(keys) * step + 1200


JOURNEYS = {
    'home': ('', 2500),
    'cai-dat-doc': seq(['UP', 'RIGHT', 'RIGHT', 'CONFIRM']),
    'ban-phim-ten-may': seq(['UP'] + ['RIGHT'] * 7 + ['CONFIRM', 'RIGHT', 'CONFIRM']),
    'file': seq(['DOWN', 'CONFIRM']),
}


def jobs():
    out = []
    for mode, tier, lang in itertools.product((0, 1), (0, 1, 2), ('VI', 'EN')):
        extra = {'tenorButtonSymbols': mode}
        for name, (script, shot) in JOURNEYS.items():
            out.append((f'{name} nhan {"chu" if mode == 0 else "ky hieu"}', tier, lang, script, {'a': shot}, {}, None, extra))
        for state, script, shots, env, saved in plan('x3', tier, 0):
            out.append((f'guifile {state} nhan {"chu" if mode == 0 else "ky hieu"}', tier, lang, script, shots, env, saved, extra))
    return out


def one(job):
    name, tier, lang, script, shots, env, saved, extra = job
    imgs, log = run_case('x3', tier, lang, 1, 0, name, script, shots, env, saved, next(_port), extra=extra)
    found = []
    for shot, im in imgs.items():
        bar = bar_of(log)  # man Home khong co thanh path: chi do pin va dong ho
        found += [(name, shot, tier, lang, i) for i in audit(im, tier, bar)]
    miss = [(name, s, tier, lang) for s in shots if s not in imgs]
    return found, miss


class HintLabels(unittest.TestCase):
    def test_labels_clear_of_battery_clock_and_bar(self):
        self.assertTrue(program('x3').exists(), 'chua dung gia lap: pio run -e simulator_x3_uc8279')
        with ThreadPoolExecutor(6) as ex:
            res = list(ex.map(one, jobs()))
        bad = [f for found, _ in res for f in found]
        miss = [m for _, ms in res for m in ms]
        for name, shot, tier, lang, (kind, detail, box) in bad:
            print(f'TRUOT {name}-{shot} co chu {tier} {lang}: {kind} {detail} {box}')
        self.assertEqual(miss, [], 'khong chup duoc anh')
        self.assertEqual(bad, [], f'{len(bad)} nhan dai nut sat hoac dinh')


if __name__ == '__main__':
    unittest.main()
