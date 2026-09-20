"""Keep every reader weight downloadable without filename collisions."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('font_manifest_generator', REPO / 'scripts/generate-font-manifest.py')
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


class DownloadManifestTest(unittest.TestCase):
    def fixture(self, path):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(struct.pack('<8sHHB19x', b'CPFONT\0\0', 4, 1, 1) + bytes(32))
        return path

    def test_all_physical_levels_keep_unique_download_names(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            paths = [self.fixture(root / 'Example' / (f'weight-{w}' if w else '') / 'Example_14.cpfont') for w in range(5)]
            manifest = generator.build_manifest({'Example': paths}, 'https://example.test/')
            names = [f['name'] for f in manifest['families'][0]['files']]
            self.assertEqual(set(names), {'Example_14.cpfont'} | {f'weight-{w}/Example_14.cpfont' for w in range(1, 5)})

    def test_duplicate_download_targets_fail_closed(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            a = self.fixture(root / 'a' / 'Example_14.cpfont')
            b = self.fixture(root / 'b' / 'Example_14.cpfont')
            with self.assertRaises(ValueError):
                generator.build_manifest({'Example': [a, b]}, 'https://example.test/')

    def test_unknown_weight_directory_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            path = self.fixture(Path(temp) / 'weight-9' / 'Example_14.cpfont')
            with self.assertRaises(ValueError):
                generator.build_manifest({'Example': [path]}, 'https://example.test/')


if __name__ == '__main__':
    unittest.main()
