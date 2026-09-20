import importlib.util
import os
from pathlib import Path
import sys
import struct
import unittest

import numpy as np

REPO = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('build_reader_weights', REPO / 'scripts/build_reader_weights.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
if os.environ.get('CROSSPOINT_MUTATE_READER_INK_GUARD') == '1':
    builder.accept_candidate = lambda prior, candidate: True


def glyph(values, advance=160):
    a = np.array(values, dtype=np.uint8)
    packed = builder.pack_pixels(a)
    return (a.shape[1], a.shape[0], advance, 0, a.shape[0], len(packed), 0), packed


class BuilderTest(unittest.TestCase):
    def test_preserve_counter(self):
        ring = glyph([[3, 3, 3], [3, 0, 3], [3, 3, 3]])
        filled = glyph([[3, 3, 3], [3, 3, 3], [3, 3, 3]])
        self.assertFalse(builder.accept_candidate(builder.glyph_metrics(ring), builder.glyph_metrics(filled)))

    def test_preserve_detached_accent(self):
        marked = glyph([[0, 3, 0], [0, 0, 0], [0, 3, 0]])
        merged = glyph([[0, 3, 0], [0, 3, 0], [0, 3, 0]])
        self.assertFalse(builder.accept_candidate(builder.glyph_metrics(marked), builder.glyph_metrics(merged)))

    def test_retains_coverage_and_bw_distinction(self):
        before = builder.glyph_metrics(glyph([[1, 3], [0, 3]]))
        stronger = builder.glyph_metrics(glyph([[2, 3], [1, 3]]))
        lighter = builder.glyph_metrics(glyph([[0, 3], [0, 3]]))
        self.assertTrue(builder.accept_candidate(before, stronger))
        self.assertFalse(builder.accept_candidate(before, lighter))

    def test_pack_roundtrip_odd_width(self):
        before = glyph([[0, 1, 2], [3, 2, 1], [0, 3, 0]])
        np.testing.assert_array_equal(builder.unpack(before), np.array([[0, 1, 2], [3, 2, 1], [0, 3, 0]]))

    def fixture_pack(self):
        original = glyph([[0, 1, 2], [3, 2, 1]])
        header = struct.pack('<8sHHB19s', b'CPFONT\0\0', 4, 1, 1, bytes(19))
        toc = builder.TOC.pack(0, 1, 1, 22, 18, -4, 0, 0, 0, 0, 0, 64, 0)
        data = header + toc + struct.pack('<III', 65, 65, 0)
        data += builder.GLYPH.pack(*original[0]) + original[1]
        return data, original

    def test_serialized_roundtrip_and_replacement(self):
        data, original = self.fixture_pack()
        header, styles = builder.read_pack(data)
        self.assertEqual(builder.encode_pack(header, styles, {(0, 65): original}), data)
        stronger = glyph([[1, 2, 3, 1], [3, 3, 2, 1], [0, 1, 0, 0]])
        result = builder.encode_pack(header, styles, {(0, 65): stronger})
        _, decoded = builder.read_pack(result)
        cp, record, bitmap = decoded[0]['records'][0]
        self.assertEqual(cp, 65)
        self.assertEqual(record, stronger[0])
        self.assertEqual(bitmap, stronger[1])
        self.assertEqual(decoded[0]['intervals'], styles[0]['intervals'])
        self.assertEqual(decoded[0]['tables'], styles[0]['tables'])
        self.assertEqual(decoded[0]['toc'], styles[0]['toc'])

    def test_serialization_rejects_advance_drift_and_bad_bitmap(self):
        data, original = self.fixture_pack()
        header, styles = builder.read_pack(data)
        with self.assertRaises(ValueError):
            builder.encode_pack(header, styles, {(0, 65): glyph([[3]], advance=161)})
        with self.assertRaises(ValueError):
            builder.read_pack(data[:-1])

    def test_empty_glyph(self):
        empty = ((0, 0, 80, 0, 0, 0, 0), b'')
        self.assertEqual(builder.glyph_metrics(empty), {'topology': (0, 0), 'ink': 0, 'coverage': 0})


if __name__ == '__main__':
    unittest.main()
