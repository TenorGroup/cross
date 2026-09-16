import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('package_ota', REPO / 'scripts/package_tenor_ota.py')
ota = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ota)


def image(version='0.1.0', chip=5, tag=b'CROSSPOINT-BOARD-V1:x4;', digest=True, sdk_version='1.6.0-19-gc33a8b0'):
    header = bytearray(24)
    header[0:2] = bytes([0xE9, 1])
    struct.pack_into('<H', header, 12, chip)
    header[23] = int(digest)
    segment = bytearray(512)
    struct.pack_into('<I', segment, 0, 0xABCD5432)
    segment[16:48] = sdk_version.encode().ljust(32, b'\0')
    segment[256:256+len(tag)] = tag
    release = b'TENOR-CROSS-VERSION-V1:' + version.encode() + b';'
    segment[320:320+len(release)] = release
    data = header + struct.pack('<II', 0x3C000020, len(segment)) + segment
    checksum = 0xEF
    for byte in segment:
        checksum ^= byte
    data += bytes(15 - len(data) % 16) + bytes([checksum])
    if digest:
        data += hashlib.sha256(data).digest()
    return data


class PackageTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.binary = self.root / 'app.bin'

    def pack(self, data):
        self.binary.write_bytes(data)
        with contextlib.redirect_stdout(io.StringIO()):
            return ota.package(self.binary, self.root / 'out')

    def test_valid_and_repeat_are_identical_without_activating(self):
        for digest in (False, True):
            with self.subTest(digest=digest):
                data = image(version=f'0.1.{int(digest)}', digest=digest)
                result = self.pack(data)
                self.assertEqual(self.pack(data), result)
                asset = result['assets'][0]
                self.assertEqual(asset['digest'], 'sha256:' + hashlib.sha256(data).hexdigest())
                self.assertEqual(asset['size'], len(data))
                self.assertFalse((self.root / 'out/firmware/stable.json').exists())

    def test_uses_product_version_instead_of_sdk_descriptor(self):
        result = self.pack(image(version='0.1.0', sdk_version='9.9.9'))
        self.assertEqual(result['tag_name'], '0.1.0')

    def test_first_release_keeps_v_prefix(self):
        result = self.pack(image(version='v1.0.0'))
        self.assertEqual(result['tag_name'], 'v1.0.0')
        self.assertTrue(result['assets'][0]['browser_download_url'].endswith('/v1.0.0/tenor-cross-v1.0.0-x3-x4.bin'))

    def test_reject_wrong_chip_board_and_release(self):
        for data in (image(chip=9), image(tag=b'CROSSPOINT-BOARD-V1:x4pro;'),
                     image(version='0.1.0-rc'), image(version='01.0.0'),
                     image(version='100000.0.0')):
            with self.subTest(data=data[:80]):
                with self.assertRaises(ValueError):
                    self.pack(data)

    def test_reject_corrupt_structure_checksum_digest_and_trailing_data(self):
        good = image()
        variants = [good[:-1], good + b'garbage', good[:80]]
        for offset in (1, 23, 28, 32, 300, len(good)-33, len(good)-1):
            bad = good.copy()
            bad[offset] ^= 0xFF
            variants.append(bad)
        for data in variants:
            with self.subTest(length=len(data), sha=hashlib.sha256(data).hexdigest()):
                with self.assertRaises(ValueError):
                    self.pack(data)

    def test_reject_conflicting_immutable_version(self):
        self.pack(image())
        with self.assertRaises(ValueError):
            self.pack(image(digest=False))

    def test_reject_oversized_image(self):
        with self.assertRaises(ValueError):
            self.pack(image() + bytes(0x640000))


if __name__ == '__main__':
    unittest.main()
