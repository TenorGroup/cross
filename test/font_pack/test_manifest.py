import hashlib
import importlib.util
import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
import zlib
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'scripts/generate-font-manifest.py'
spec = importlib.util.spec_from_file_location('font_manifest', SCRIPT)
manifest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manifest)


class ManifestTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir=ROOT.parent)
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.header = struct.pack('<8sHHB19x', b'CPFONT\0\0', 4, 1, 1) + bytes(32)
        self.font = self.directory / 'Small_12.cpfont'
        self.font.write_bytes(self.header)
        self.pack = self.directory / 'Small.cpfontpack'
        self.write_pack()

    def write_pack(self, family='Small', compression=zipfile.ZIP_STORED):
        with zipfile.ZipFile(self.pack, 'w', compression=compression) as archive:
            archive.writestr('pack.json', json.dumps({'family': family, 'format': 1,
                                                     'cpfont_version': 4, 'sizes': [12]}))
            archive.writestr('Small_12.cpfont', self.header)

    def test_pack_schema_hashes(self):
        result = manifest.build_manifest({'Small': [self.pack]}, 'https://example.com/fonts/', True)
        family = result['families'][0]
        self.assertEqual(family['styles'], ['regular'])
        self.assertEqual(len(family['files']), 1)
        entry = family['files'][0]
        payload = self.pack.read_bytes()
        self.assertEqual(entry['name'], 'Small.cpfontpack')
        self.assertEqual(entry['url'], result['baseUrl'] + entry['name'])
        self.assertEqual(entry['size'], len(payload))
        self.assertEqual(entry['sha256'], hashlib.sha256(payload).hexdigest())
        if os.environ.get('FONT_MANIFEST_INJECT_CRC_BUG'):
            entry['crc32'] ^= 1
        self.assertEqual(entry['crc32'], zlib.crc32(payload))

    def test_legacy_schema_unchanged(self):
        result = manifest.build_manifest({'Small': [self.font]}, 'https://example.com/')
        self.assertEqual(result, {'version': 1, 'baseUrl': 'https://example.com/', 'scriptGroups': [],
                                 'families': [{'name': 'Small', 'description': 'Small', 'styles': ['regular'],
                                               'scripts': [], 'files': [{'name': 'Small_12.cpfont',
                                                'size': len(self.header), 'crc32': zlib.crc32(self.header)}]}]})

    def test_cli_pack_copy_and_trailing_slash(self):
        destination = self.directory / 'assets'
        output = self.directory / 'fonts.json'
        subprocess.run([sys.executable, '-B', str(SCRIPT), '--packs', '--input', str(self.directory),
                        '--base-url', 'https://example.com/fonts', '--output', str(output),
                        '--assets-output', str(destination)], check=True, capture_output=True)
        result = json.loads(output.read_text())
        self.assertEqual(result['baseUrl'], 'https://example.com/fonts/')
        self.assertEqual((destination / self.pack.name).read_bytes(), self.pack.read_bytes())

    def test_duplicate_family_rejected(self):
        with self.assertRaisesRegex(ValueError, 'one pack'):
            manifest.build_manifest({'Small': [self.pack, self.pack]}, 'https://example.com/', True)

    def test_mismatched_family_rejected(self):
        self.write_pack(family='Other')
        with self.assertRaisesRegex(ValueError, 'metadata'):
            manifest.build_manifest({'Small': [self.pack]}, 'https://example.com/', True)

    def test_deflate_rejected(self):
        self.write_pack(compression=zipfile.ZIP_DEFLATED)
        with self.assertRaisesRegex(ValueError, 'ZIP STORE'):
            manifest.build_manifest({'Small': [self.pack]}, 'https://example.com/', True)

    def test_bad_name_rejected(self):
        with self.assertRaisesRegex(ValueError, 'family name'):
            manifest.build_manifest({'../Small': [self.pack]}, 'https://example.com/', True)

    def test_invalid_crc_rejected(self):
        with patch.object(zipfile.ZipFile, 'testzip', return_value='Small_12.cpfont'):
            with self.assertRaisesRegex(ValueError, 'ZIP CRC'):
                manifest.build_manifest({'Small': [self.pack]}, 'https://example.com/', True)


if __name__ == '__main__':
    unittest.main()
