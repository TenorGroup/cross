"""X3, ban phim (nhap mat khau Wi-Fi va ten may; man ban phim dung chung): vong chon cua phim dau hang va phim cuoi hang
cach mep man >= 8 px. Do tren anh 1 bit: khoi vong (ring) cua phim dang chon, o phim dau va sau 9 lan bam DOWN (X3 mac dinh doi truc: DOWN sang phai 1 phim).
Chay: GUIFILE_X3_PROGRAM=... python3 test/gui_file_overlap/test_g3_keyboard_margin_x3.py"""
import itertools
import sys
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from measure import segment
from plan import plan, program, run_case

_port = itertools.count(30600)
MARGIN = 8


def seq(keys, t0=1400, step=1000):
    return [f'{t0 + i * step}:{k}' for i, k in enumerate(keys)], t0 + len(keys) * step


def cases(tier):
    out = {}
    for state, script, shots, env, saved in plan('x3', tier, 0):
        if state == 'sta-password':
            t = shots['password']
            moves = [f'{t + 500 + 900 * i}:DOWN' for i in range(9)]
            out['mat-khau-wifi'] = (script + ';' + ';'.join(moves), {'dau': t, 'cuoi': t + 500 + 900 * 9 + 600}, env, None)
    ev, t = seq(['UP'] + ['RIGHT'] * 7 + ['CONFIRM', 'RIGHT', 'CONFIRM'])
    moves = [f'{t + 1500 + 900 * i}:DOWN' for i in range(9)]
    out['ten-may'] = (';'.join(ev + moves), {'dau': t + 1200, 'cuoi': t + 1500 + 900 * 9 + 600}, {}, None)
    return out


def one(job):
    tier, lang, name, (script, shots, env, saved) = job
    imgs, _ = run_case('x3', tier, lang, 0, 0, name, script, shots, env, saved, next(_port))
    found = []
    for shot, im in imgs.items():
        blocks, _m = segment(im, 'x3')
        W = im.width
        rings = [b for b in blocks if b.kind == 'ring' and b.y0 > 40 and b.y1 < im.height - 120]  # bo cac vien tron cua thanh path
        if not rings:
            found.append(f'khong thay vong chon: {name} {shot} co chu {tier} {lang}')
            continue
        left, right = min(b.x0 for b in rings), W - max(b.x1 for b in rings)
        if left < MARGIN or right < MARGIN:
            found.append(f'vong chon sat mep: {name} {shot} co chu {tier} {lang}: trai {left}, phai {right}')
    return found + ([] if imgs.keys() >= shots.keys() else [f'thieu anh {name} {tier} {lang}'])


class KeyboardMarginX3(unittest.TestCase):
    def test_selection_ring_clear_of_edge(self):
        self.assertTrue(program('x3').exists(), 'chua dung gia lap: pio run -e simulator_x3_uc8279')
        jobs = [(t, lang, n, c) for t in (0, 1, 2) for lang in ('VI', 'EN') for n, c in cases(t).items()]
        with ThreadPoolExecutor(6) as ex:
            bad = [m for found in ex.map(one, jobs) for m in found]
        for line in bad:
            print('TRUOT', line)
        self.assertEqual(bad, [])


if __name__ == '__main__':
    unittest.main()
