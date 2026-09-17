"""Mac dinh cua trinh doc va bang di cu space (textSpacingVersion 3).

Bang nay la bang da do thuc thi trong `t1/run_migration_fixtures.py` (xem
checkpoint T1, muc "Migration executed end to end"). Bon loai gian dung CHUNG mot
enum nam muc: 0 Mac dinh, 1 Sieu hep, 2 Hep, 3 Rong, 4 Sieu rong; file cu duoc quy
doi theo y dinh cu roi ghi lai voi version 3.
"""

import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))

MAC_DINH, SIEU_HEP, HEP, RONG, SIEU_RONG = range(5)

# Fixture: chi ghi cac khoa lien quan toi gian chu; phan con lai lay mac dinh.
FIXTURES = {
    'v1.0.2 (khong co version, khong co gian chu)': (
        {'lineSpacing': 0, 'extraParagraphSpacing': 1},
        {'lineSpacing': HEP, 'letterSpacing': MAC_DINH, 'extraParagraphSpacing': RONG},
    ),
    'v2 (co version 2 va gian chu)': (
        {'lineSpacing': 0, 'letterSpacing': 2, 'extraParagraphSpacing': 1, 'textSpacingVersion': 2},
        {'lineSpacing': HEP, 'letterSpacing': RONG, 'extraParagraphSpacing': RONG},
    ),
    'khong co version nhung da co gian doan': (
        {'lineSpacing': 2, 'letterSpacing': 0, 'extraParagraphSpacing': 1},
        {'lineSpacing': RONG, 'letterSpacing': HEP, 'extraParagraphSpacing': RONG},
    ),
    'v3 (giu nguyen muc nguoi dung chon)': (
        {'lineSpacing': 4, 'letterSpacing': 1, 'wordSpacing': 3, 'extraParagraphSpacing': 0,
         'textSpacingVersion': 3},
        {'lineSpacing': SIEU_RONG, 'letterSpacing': SIEU_HEP, 'extraParagraphSpacing': MAC_DINH},
    ),
    'v3 nhung gia tri ngoai pham vi': (
        {'lineSpacing': 99, 'letterSpacing': 250, 'wordSpacing': 7, 'extraParagraphSpacing': 200,
         'textSpacingVersion': 3},
        {'lineSpacing': MAC_DINH, 'letterSpacing': MAC_DINH, 'extraParagraphSpacing': MAC_DINH},
    ),
}


class ReaderDefaultsTest(unittest.TestCase):
    def boot(self, settings: dict) -> dict:
        """Khoi dong may mo phong voi mot the nho dung mot lan roi doc lai file cai dat."""
        with tempfile.TemporaryDirectory(prefix='reader-defaults-') as folder:
            sd = Path(folder)
            store = sd / '.crosspoint'
            store.mkdir()
            (store / 'settings.json').write_text(json.dumps(dict(settings, language='VI', uiTheme=4)))
            env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
            env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT='1500:QUIT')
            result = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertNotIn('page buffer slots full', result.stdout + result.stderr)
            return json.loads((store / 'settings.json').read_text())

    def test_mac_dinh_bon_loai_gian_deu_o_muc_mac_dinh(self):
        saved = self.boot({})
        for key in ('lineSpacing', 'letterSpacing', 'wordSpacing', 'extraParagraphSpacing'):
            self.assertEqual(saved[key], MAC_DINH, key)
        self.assertEqual(saved['textSpacingVersion'], 3)
        self.assertEqual(saved['fontSize'], 16)

    def test_bang_di_cu(self):
        for ten, (vao, ra) in FIXTURES.items():
            with self.subTest(fixture=ten):
                saved = self.boot(vao)
                for key, mong_doi in ra.items():
                    self.assertEqual(saved[key], mong_doi, f'{ten}: {key}')
                self.assertEqual(saved['textSpacingVersion'], 3, ten)

    def test_nam_muc_deu_ghi_va_doc_lai_duoc(self):
        """Ghi lan luot nam muc cho ca bon loai roi doc lai: khong muc nao bi ep ve mac dinh."""
        for muc in range(5):
            with self.subTest(muc=muc):
                saved = self.boot({'lineSpacing': muc, 'letterSpacing': muc, 'wordSpacing': muc,
                                   'extraParagraphSpacing': muc, 'textSpacingVersion': 3})
                for key in ('lineSpacing', 'letterSpacing', 'wordSpacing', 'extraParagraphSpacing'):
                    self.assertEqual(saved[key], muc, f'{key} o muc {muc}')


if __name__ == '__main__':
    unittest.main()
