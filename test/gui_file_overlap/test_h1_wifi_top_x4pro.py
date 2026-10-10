"""X4 Pro, danh sach Wi-Fi, man loi noi va man dang noi: dau man chi co hang trang thai; khong con hang "Dia chi MAC" kem vach
ngang. Dia chi MAC la dong dau cua noi dung (cach hang trang thai INFO_TOP_GAP), cat bang "..." neu dai, khong de len gi.
Chay: GUIFILE_X4PRO_PROGRAM=... python3 test/gui_file_overlap/test_h1_wifi_top_x4pro.py"""
import itertools
import sys
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from measure import measure, segment
from plan import plan, program, run_case

_port = itertools.count(31000)
STATES = ('sta-list', 'sta-fail', 'sta-timeout')


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
        mac = [b for b in blocks if b.kind == 'text' and 38 <= b.y0 <= 70 and b.x0 < 40 and b.w > 120]
        if not mac:
            bad.append(f'khong thay dong MAC o dau noi dung: {tag}')
        issues, _b = measure(im, 'x4pro')
        bad += [f'{i["kind"]} {i["detail"]} {i["boxes"]}: {tag}' for i in issues]
    return bad


class WifiTopX4Pro(unittest.TestCase):
    def test_status_strip_only(self):
        self.assertTrue(program('x4pro').exists(), 'chua dung gia lap: pio run -e simulator_x4pro')
        jobs = []
        for tier, lang, long in itertools.product((0, 1, 2), ('VI', 'EN'), (0, 1)):
            for state, script, shots, env, saved in plan('x4pro', tier, long):
                if state in STATES:
                    jobs.append((tier, lang, long, state, script, shots, env, saved))
        with ThreadPoolExecutor(6) as ex:
            bad = [m for found in ex.map(one, jobs) for m in found]
        for line in bad:
            print('TRUOT', line)
        self.assertEqual(bad, [])


if __name__ == '__main__':
    unittest.main()
