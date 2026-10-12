import contextlib
import copy
import hashlib
import importlib.util
import io
import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'lib/EpdFont/scripts/build-sd-fonts.py'
spec = importlib.util.spec_from_file_location('sd_builder', SCRIPT)
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
sys.path.insert(0, str(ROOT / 'scripts'))
import build_font_pack as pack


class MaliRecipeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        config = yaml.safe_load((SCRIPT.parent / 'sd-fonts.yaml').read_text())
        cls.family = next((f for f in config['families'] if f['name'] == 'Mali'), None)
        if cls.family is None:
            raise AssertionError('Mali recipe is missing from sd-fonts.yaml')
        cls.temporary = tempfile.TemporaryDirectory(dir=ROOT.parent)
        cls.directory = Path(cls.temporary.name)
        with contextlib.redirect_stderr(io.StringIO()):
            name, ok, error = builder.build_family(cls.family, cls.directory)
        if not ok:
            cls.temporary.cleanup()
            raise AssertionError(error)
        cls.output = cls.directory / name / (name + '.cpfontpack')

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_four_sizes_styles_and_six_levels(self):
        self.assertEqual(self.family['sizes'], [12, 14, 16, 18])
        self.assertEqual(set(self.family['styles']), set(pack.STYLE_NAMES))
        with zipfile.ZipFile(self.output) as archive:
            metadata = json.loads(archive.read('pack.json'))
            fonts = [e for e in metadata['entries'] if e['path'].endswith('.cpfont')]
            self.assertEqual(len(fonts), 24)
            for entry in fonts:
                data = archive.read(entry['path'])
                quality = pack.reading_quality(data, 1.2)
                self.assertEqual(entry['reading_quality'], quality)
                self.assertEqual([s['toc'][0] for s in pack.read_font(data)[1]], [0,1,2,3])
                self.assertEqual(hashlib.sha256(data).hexdigest(), entry['sha256'])
                self.assertEqual(len(data), entry['size'])
                base = archive.read(f'Mali_{entry["point_size"]}.cpfont')
                pack.assert_same_layout(base, data)
            self.assertIn(b'Copyright', archive.read('OFL.txt'))
            self.assertIn(b'Fallback font license', archive.read('OFL.txt'))

    def test_sources_are_pinned(self):
        sources = list(self.family['styles'].values()) + [self.family['license'], self.family['fallback']]
        for source in sources:
            path = builder.EPDFONTS_DIR / source['path']
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), source['sha256'])
        for source in self.family['styles'].values():
            self.assertIn('bd8f81ddb5c74d5c8897b36ad88b440266245103', source['url'])

    def test_wrong_source_hash_is_rejected(self):
        family = copy.deepcopy(self.family)
        family['styles']['regular']['sha256'] = '0' * 64
        _, ok, error = builder.build_family(family, self.directory / 'bad')
        self.assertFalse(ok)
        self.assertIn('hash mismatch', error)

    def test_fallback_contains_only_replacement(self):
        from fontTools.ttLib import TTFont
        with TTFont(builder.EPDFONTS_DIR / self.family['fallback']['path']) as font:
            self.assertEqual(set(font.getBestCmap()), {0xFFFD})


if __name__ == '__main__':
    unittest.main()
