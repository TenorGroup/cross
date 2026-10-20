#!/usr/bin/env python3
from __future__ import annotations

import contextlib
import io
import json
import os
import re
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

FONT_ROOT = ROOT / 'lib/EpdFont/builtinFonts/source/NotoSans'


class InkStrengthContractTests(unittest.TestCase):
    def test_firmware_and_builder_use_the_approved_ramp(self):
        header = (ROOT / 'src/ReaderInkWeight.h').read_text()
        table = re.search(r'OUTLINE_STRENGTHS\s*\[\s*LEVEL_COUNT\s*\]\s*=\s*\{([^}]+)\}', header)
        self.assertIsNotNone(table)
        stored = tuple(int(value.strip()) for value in table.group(1).split(','))
        # Stored level 3 is "0"; the firmware keeps -3..+5, the pack keeps 0, +1..+5, then -1, -2, -3.
        # v1.0.69: steps chosen so neighbours stay >= 5% apart in 2-bit coverage (dev/dam/phu2bit).
        self.assertEqual(stored, (-48, -32, -18, 0, 25, 37, 63, 81, 96))
        self.assertEqual(pack.STRENGTHS, stored[3:] + tuple(reversed(stored[:3])))
        self.assertEqual(pack.RECIPE, 'reader-outline-v5-even')


class FontPackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='font-pack-test-', dir=ROOT.parent)
        cls.directory = Path(cls.temporary.name)
        cls.fonts = {0: str(FONT_ROOT / 'NotoSans-Regular.ttf')}
        cls.intervals = pack.builtin_intervals()
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

    def test_missing_reading_glyph_rejected(self):
        for codepoint in (0x1EF1, 0x0306, 0xFFFD):
            intervals = [(cp, cp) for low, high in self.intervals
                         for cp in range(low, high + 1) if cp != codepoint]
            output = self.directory / 'missing.cpfontpack'
            with self.subTest(codepoint=codepoint):
                with self.assertRaisesRegex(ValueError, f'U\\+{codepoint:04X}'):
                    pack.build_pack('Small', self.fonts, [12], pack.STRENGTHS, intervals,
                                    FONT_ROOT / 'OFL.txt', output, pnum=True)
                self.assertFalse(output.exists())

    def test_empty_reading_glyph_rejected(self):
        original = pack.weights.RasterSource.glyph

        def empty(source, codepoint, strength):
            glyph, bitmap = original(source, codepoint, strength)
            if codepoint == 0x1EF1 and strength:
                bitmap = bytes(len(bitmap))
            return glyph, bitmap

        with patch.object(pack.weights.RasterSource, 'glyph', empty):
            with self.assertRaisesRegex(ValueError, 'Empty reading glyph.*U\\+1EF1'):
                self.build(self.directory / 'empty.cpfontpack')

    def test_non_ofl_license_rejected(self):
        license_path = self.directory / 'wrong-license.txt'
        license_path.write_text('A font with no redistribution license')
        with self.assertRaisesRegex(ValueError, 'OFL 1.1'):
            pack.build_pack('Small', self.fonts, [12], pack.STRENGTHS, self.intervals,
                            license_path, self.directory / 'unlicensed.cpfontpack')

    def test_missing_license_rejected(self):
        with self.assertRaises(FileNotFoundError):
            pack.build_pack('Small', self.fonts, [12], pack.STRENGTHS, self.intervals,
                            self.directory / 'absent-OFL.txt', self.directory / 'absent.cpfontpack')

    def test_ink_crowding_rejected(self):
        original = pack.weights.RasterSource.glyph

        def raised(source, codepoint, strength):
            glyph, bitmap = original(source, codepoint, strength)
            if codepoint == 0x1EF1 and strength:
                glyph = list(glyph)
                glyph[4] += 60
            return tuple(glyph), bitmap

        with patch.object(pack.weights.RasterSource, 'glyph', raised):
            with self.assertRaisesRegex(ValueError, 'Line spacing'):
                self.build(self.directory / 'crowded.cpfontpack')

    def test_repertoire_counts_and_line_gap(self):
        nfc, nfd = pack.vietnamese_repertoire()
        self.assertEqual((len(nfc), len(nfd)), (178, 54))
        for entry in self.metadata['entries']:
            if entry['path'].endswith('.cpfont'):
                self.assertGreaterEqual(entry['reading_quality']['gap_pixels'], 1)

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
                         ['', 'weight-2', 'weight-3', 'weight-4', 'weight-5', 'weight-6', 'weight-7', 'weight-8',
                          'weight-9'])
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
            for level in range(9):
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

    def test_thin_levels_remove_ink_and_keep_the_layout(self):
        with zipfile.ZipFile(self.output) as archive:
            base = archive.read('Small_12.cpfont')
            ink = {0: pack.ink_coverage(base)}
            for level, directory in ((6, 'weight-7'), (7, 'weight-8'), (8, 'weight-9')):
                data = archive.read(f'{directory}/Small_12.cpfont')
                pack.assert_same_layout(base, data)
                ink[pack.STRENGTHS[level]] = pack.ink_coverage(data)
            ink[pack.STRENGTHS[1]] = pack.ink_coverage(archive.read('weight-2/Small_12.cpfont'))
        thin1, plus1 = pack.STRENGTHS[6], pack.STRENGTHS[1]
        self.assertLess(ink[-48], ink[-32])
        self.assertLess(ink[-32], ink[thin1])
        self.assertLess(ink[thin1], ink[0])
        self.assertLess(ink[0], ink[plus1])
        # The three thin steps are about as wide as the +1 step is: not one tiny nudge, not a collapse.
        self.assertGreater(ink[-48] / ink[0], 0.5)
        self.assertLess(ink[thin1] / ink[0], 0.95)

    def test_a_thin_level_with_more_ink_than_the_base_is_rejected(self):
        original = pack.weights.RasterSource.glyph

        def flipped(source, codepoint, strength):
            return original(source, codepoint, abs(strength))

        with patch.object(pack.weights.RasterSource, 'glyph', flipped):
            with contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaisesRegex(ValueError, 'Ink ramp not increasing'):
                    self.build(self.directory / 'flipped.cpfontpack')
        self.assertFalse((self.directory / 'flipped.cpfontpack').exists())

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
            self.assertEqual(len(archive.namelist()), 11)
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
            if strength == pack.STRENGTHS[2]:
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
        rows = pack.compare_builtin(reference.read_bytes(), 'notosans', 12)
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
