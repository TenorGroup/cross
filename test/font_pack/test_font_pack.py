#!/usr/bin/env python3
from __future__ import annotations

import contextlib
import io
import json
import os
import struct
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import build_font_pack as pack

FONT_ROOT = ROOT / 'lib/EpdFont/builtinFonts/source/NotoSerif'


class FontPackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='font-pack-test-', dir=ROOT.parent)
        cls.directory = Path(cls.temporary.name)
        cls.fonts = {0: str(FONT_ROOT / 'NotoSerif-Regular.ttf')}
        cls.intervals = [(0x20, 0x7E), (0x1EA0, 0x1EF9)]
        cls.output = cls.directory / 'Small.cpfontpack'
        with contextlib.redirect_stderr(io.StringIO()):
            cls.metadata = cls.build(cls.output)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    @classmethod
    def build(cls, output):
        return pack.build_pack('Small', cls.fonts, [12], pack.STRENGTHS, cls.intervals,
                               FONT_ROOT / 'OFL.txt', output, pnum=True)

    def test_extract_and_hash(self):
        with zipfile.ZipFile(self.output) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(archive.namelist(), sorted(archive.namelist()))
            for entry in archive.infolist():
                self.assertEqual(entry.compress_type, zipfile.ZIP_STORED)
                self.assertEqual(entry.date_time, pack.ZIP_TIME)
            archive.extractall(self.directory / 'unpacked')
            metadata = json.loads(archive.read('pack.json'))
        self.assertEqual(metadata['recipe'], pack.RECIPE)
        self.assertEqual([level['directory'] for level in metadata['levels']],
                         ['', 'weight-2', 'weight-3', 'weight-4', 'weight-5', 'weight-6'])
        self.assertEqual(metadata['legacy_compatible_directory'], 'weight-1')
        for entry in metadata['entries']:
            content = (self.directory / 'unpacked' / entry['path']).read_bytes()
            self.assertEqual(len(content), entry['size'])
            self.assertEqual(pack.weights.sha(content), entry['sha256'])
            if entry['path'].endswith('.cpfont'):
                pack.read_font(content)

    def test_advance_and_metadata_invariant(self):
        with zipfile.ZipFile(self.output) as archive:
            base = archive.read('Small_12.cpfont')
            for level in range(6):
                candidate = bytearray(archive.read(pack.entry_name('Small', 12, level)))
                if os.environ.get('FONT_PACK_INJECT_ADVANCE_BUG') and level == 1:
                    _, styles = pack.read_font(candidate)
                    glyph_offset = styles[0]['toc'][11] + styles[0]['toc'][1] * 12
                    old_advance = struct.unpack_from('<H', candidate, glyph_offset + 2)[0]
                    struct.pack_into('<H', candidate, glyph_offset + 2, old_advance + 1)
                pack.assert_same_layout(base, candidate)
            _, base_styles = pack.read_font(base)
            _, strongest = pack.read_font(archive.read('weight-6/Small_12.cpfont'))
            self.assertTrue(any(old[1][:2] != new[1][:2] or old[2] != new[2]
                                for old, new in zip(base_styles[0]['records'], strongest[0]['records'])))

    def test_published_base_preserved(self):
        with zipfile.ZipFile(self.output) as archive:
            base = archive.read('Small_12.cpfont')
        baseline = self.directory / 'base'
        baseline.mkdir(exist_ok=True)
        (baseline / 'Small_12.cpfont').write_bytes(base)
        output = self.directory / 'Preserved.cpfontpack'
        pack.build_pack('Small', self.fonts, [12], pack.STRENGTHS, self.intervals,
                        FONT_ROOT / 'OFL.txt', output, pnum=True, base_dir=baseline)
        with zipfile.ZipFile(output) as archive:
            self.assertEqual(archive.read('Small_12.cpfont'), base)

    def test_wrong_published_source_rejected(self):
        with zipfile.ZipFile(self.output) as archive:
            base = bytearray(archive.read('Small_12.cpfont'))
        _, styles = pack.read_font(base)
        offset = styles[0]['toc'][11] + styles[0]['toc'][1] * 12 + 2
        struct.pack_into('<H', base, offset, struct.unpack_from('<H', base, offset)[0] + 1)
        baseline = self.directory / 'wrong-base'
        baseline.mkdir(exist_ok=True)
        (baseline / 'Small_12.cpfont').write_bytes(base)
        with self.assertRaisesRegex(ValueError, 'Source raster differs'):
            pack.build_pack('Small', self.fonts, [12], pack.STRENGTHS, self.intervals,
                            FONT_ROOT / 'OFL.txt', self.directory / 'Rejected.cpfontpack',
                            pnum=True, base_dir=baseline)

    def test_fallback_license_in_supported_entry(self):
        fallback = FONT_ROOT.parent / 'NotoSans'
        output = self.directory / 'Fallback.cpfontpack'
        pack.build_pack('Small', self.fonts, [12], pack.STRENGTHS, self.intervals,
                        FONT_ROOT / 'OFL.txt', output,
                        fallback_style_fonts={0: str(fallback / 'NotoSans-Regular.ttf')})
        with zipfile.ZipFile(output) as archive:
            license_data = archive.read('OFL.txt')
            self.assertIn((FONT_ROOT / 'OFL.txt').read_bytes(), license_data)
            self.assertIn((fallback / 'OFL.txt').read_bytes(), license_data)
            self.assertEqual(len(archive.namelist()), 8)
            self.assertIn('0', json.loads(archive.read('pack.json'))['fallback_sources'])

    def test_encode_rejects_advance_bug(self):
        with zipfile.ZipFile(self.output) as archive:
            header, styles = pack.read_font(archive.read('Small_12.cpfont'))
        replacements = {(style['toc'][0], codepoint): (glyph, bitmap)
                        for style in styles for codepoint, glyph, bitmap in style['records']}
        first = next(iter(replacements))
        glyph, bitmap = replacements[first]
        changed = list(glyph)
        changed[2] += 1
        replacements[first] = tuple(changed), bitmap
        with self.assertRaisesRegex(ValueError, 'preserve every glyph advance'):
            pack.weights.encode_pack(header, styles, replacements)

    def test_layout_detects_tables_and_metrics(self):
        with zipfile.ZipFile(self.output) as archive:
            base = archive.read('Small_12.cpfont')
        _, styles = pack.read_font(base)
        toc = styles[0]['toc']
        table_offset = toc[11] + toc[1] * 12 + toc[2] * pack.weights.GLYPH.size
        offsets = [32 + 12, table_offset]
        if toc[10]:
            offsets.append(table_offset + toc[6] * 3 + toc[7] * 3 + toc[8] * toc[9])
        for offset in offsets:
            with self.subTest(offset=offset):
                changed = bytearray(base)
                changed[offset] ^= 1
                with self.assertRaisesRegex(ValueError, 'Layout invariant'):
                    pack.assert_same_layout(base, changed)

    def test_strength_bug_rejected_during_build(self):
        original = pack.weights.RasterSource.glyph

        def corrupted(source, codepoint, strength):
            glyph, bitmap = original(source, codepoint, strength)
            if strength == 32:
                changed = list(glyph)
                changed[2] += 1
                return tuple(changed), bitmap
            return glyph, bitmap

        with patch.object(pack.weights.RasterSource, 'glyph', corrupted):
            with contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaisesRegex(ValueError, 'preserve every glyph advance'):
                    self.build(self.directory / 'bad.cpfontpack')
        self.assertFalse((self.directory / 'bad.cpfontpack').exists())

    def test_level_zero_byte_identical(self):
        reference = self.directory / 'reference.cpfont'
        with contextlib.redirect_stderr(io.StringIO()):
            pack.converter.generate_cpfont_multistyle(self.fonts, 12, self.intervals, str(reference), pnum=True)
        with zipfile.ZipFile(self.output) as archive:
            self.assertEqual(archive.read('Small_12.cpfont'), reference.read_bytes())

    def test_repeat_byte_identical(self):
        repeated = self.directory / 'repeat.cpfontpack'
        with contextlib.redirect_stderr(io.StringIO()):
            self.build(repeated)
        self.assertEqual(self.output.read_bytes(), repeated.read_bytes())

    def test_builtin_pnum_parity(self):
        reference = self.directory / 'builtin.cpfont'
        with contextlib.redirect_stderr(io.StringIO()):
            pack.converter.generate_cpfont_multistyle(self.fonts, 12, pack.builtin_intervals(),
                                                      str(reference), pnum=True)
        rows = pack.compare_builtin(reference.read_bytes(), 'notoserif', 12)
        self.assertGreater(rows[0]['compared'], 1000)
        self.assertEqual(rows[0]['mismatches'], 0)
        self.assertEqual(rows[0]['missing'], [])
        self.assertEqual(rows[0]['extra'], [])

    def test_invalid_recipe_and_paths(self):
        with self.assertRaisesRegex(ValueError, 'requires strength'):
            pack.build_pack('Small', self.fonts, [12], [0, 31, 64, 96, 128, 160], self.intervals,
                            FONT_ROOT / 'OFL.txt', self.directory / 'bad-recipe.cpfontpack')
        with self.assertRaisesRegex(ValueError, 'safe ASCII'):
            pack.build_pack('../Small', self.fonts, [12], pack.STRENGTHS, self.intervals,
                            FONT_ROOT / 'OFL.txt', self.directory / 'bad-path.cpfontpack')


if __name__ == '__main__':
    unittest.main()
