"""Luong Gui file (chooser, noi mang, may chu, diem phat, Calibre) khong co khoi muc nao chong len nhau tren ca 2 may.

Do tren anh 1 bit cua gia lap (khong theo rect trong ma): QR, khung/vien, chu theo dong; moi cap khoi phai cach nhau
>= 4 px, muc cach mep man >= 4 px (X3) / 8 px (X4 Pro), chu khong chui vao vung thanh, nhan nut khong dinh vao pin va dong ho.

Chay: pio run -e simulator_x3_uc8279 -e simulator_x4pro, roi python3 test/gui_file_overlap/test_gui_file_overlap.py
GUIFILE_X3_PROGRAM / GUIFILE_X4PRO_PROGRAM chon ban dung khac. GUIFILE_DEVICES=x3 hoac x4pro de chay mot may."""
import itertools
import os
import sys
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from measure import measure, parse_trace
from plan import REPO, plan, program, run_case

KNOWN = set()
# (co chu, ngon ngu, mep, ten dai): ten dai = ten may va ten mang 31 chu W
COMBOS = [(t, lang, tips, long) for t in (0, 1, 2) for lang, tips, long in (('VI', 0, 0), ('EN', 1, 1))]
_port = itertools.count(29600)


def jobs(device):
    out = []
    for tier, lang, tips, long in COMBOS:
        for state, script, shots, env, saved in plan(device, tier, long):
            out.append((device, tier, lang, tips, long, state, script, shots, env, saved, next(_port)))
    return out


def one(job):
    device, tier, lang, tips, long, state, script, shots, env, saved, port = job
    imgs, log = run_case(device, tier, lang, tips, long, state, script, shots, env, saved, port)
    found = []
    for name, im in imgs.items():
        trace = None
        if device == 'x3':
            trace = parse_trace(log, f'{shots[name]}.bmp')
        issues, _ = measure(im, device, trace, tier)
        found += [(device, state, name, tier, lang, tips, long, i) for i in issues
                  if (device, state, i['kind']) not in KNOWN]
    missing = [(device, state, n, tier, lang, tips, long) for n in shots if n not in imgs]
    return found, missing


class GuiFileOverlap(unittest.TestCase):
    def test_no_overlap(self):
        devices = [d for d in os.environ.get('GUIFILE_DEVICES', 'x3,x4pro').split(',') if program(d).exists()]
        self.assertTrue(devices, 'chua dung gia lap: pio run -e simulator_x3_uc8279 -e simulator_x4pro')
        todo = [j for d in devices for j in jobs(d)]
        with ThreadPoolExecutor(6) as ex:
            results = list(ex.map(one, todo))
        bad = [f for found, _ in results for f in found]
        missing = [m for _, miss in results for m in miss]
        for dev, state, shot, tier, lang, tips, long, i in bad:
            print(f'TRUOT {dev} {state}-{shot} co chu {tier} {lang} meo {tips} ten {"dai" if long else "ngan"}: '
                  f'{i["kind"]} {i["detail"]} {i["boxes"]}')
        self.assertEqual(missing, [], 'khong chup duoc anh')
        self.assertEqual(bad, [], f'{len(bad)} cho chong lan hoac sat nhau')


if __name__ == '__main__':
    unittest.main()
