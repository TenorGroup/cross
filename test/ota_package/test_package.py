import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('package_ota', REPO / 'scripts/package_tenor_ota.py')
ota = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ota)


def image(version='0.1.0', chip=5, tag=b'CROSSPOINT-BOARD-V1:x4;', digest=True, sdk_version='1.6.0-19-gc33a8b0', product_tag=True):
    header = bytearray(24)
    header[0:2] = bytes([0xE9, 1])
    struct.pack_into('<H', header, 12, chip)
    header[23] = int(digest)
    segment = bytearray(512)
    struct.pack_into('<I', segment, 0, 0xABCD5432)
    segment[16:48] = sdk_version.encode().ljust(32, b'\0')
    segment[256:256+len(tag)] = tag
    release = b'TENOR-CROSS-VERSION-V1:' + version.encode() + b';' if product_tag else b''
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

    def pack_pair(self, c3, s3):
        self.binary.write_bytes(c3)
        second = self.root / 'x4pro.bin'
        second.write_bytes(s3)
        with contextlib.redirect_stdout(io.StringIO()):
            return ota.package(self.binary, self.root / 'out', x4pro_binary=second)

    def test_pair_has_matching_board_digest_size_and_staging_only(self):
        c3 = image(version='v1.0.54')
        s3 = image(version='v1.0.54', chip=9, tag=b'CROSSPOINT-BOARD-V1:x4pro;')
        result = self.pack_pair(c3, s3)
        self.assertEqual(result, self.pack_pair(c3, s3))
        self.assertEqual(result['tag_name'], 'v1.0.54')
        for asset, suffix, data in zip(result['assets'], ('x3-x4', 'x4pro'), (c3, s3)):
            self.assertEqual(asset['name'], f'tenor-cross-{suffix}.bin')
            self.assertEqual(asset['size'], len(data))
            self.assertEqual(asset['digest'], 'sha256:' + hashlib.sha256(data).hexdigest())
            name = f'tenor-cross-v1.0.54-{suffix}.bin'
            self.assertEqual(asset['browser_download_url'], f'https://cross.tenor.vn/firmware/v1.0.54/{name}')
            folder = self.root / 'out/firmware/v1.0.54'
            self.assertEqual((folder / name).read_bytes(), data)
            self.assertEqual((folder / (name + '.sha256')).read_text(), hashlib.sha256(data).hexdigest() + '  ' + name + '\n')
        self.assertEqual(len(result['assets']), 2)
        self.assertEqual(len(list(folder.iterdir())), 4)
        self.assertFalse((self.root / 'out/firmware/stable.json').exists())

    def test_pair_preflight_rejects_bad_second_image_without_output(self):
        good = image(version='v1.0.54', chip=9, tag=b'CROSSPOINT-BOARD-V1:x4pro;')
        corrupt = good.copy()
        corrupt[300] ^= 1
        for bad in (image(version='v1.0.54'), image(version='v1.0.54', chip=9),
                    image(version='v1.0.53', chip=9, tag=b'CROSSPOINT-BOARD-V1:x4pro;'),
                    image(version='v1.0.54-rc.1', chip=9, tag=b'CROSSPOINT-BOARD-V1:x4pro;'),
                    good[:-1], good + bytes(0x640000), corrupt):
            with self.subTest(sha=hashlib.sha256(bad).hexdigest()):
                with self.assertRaises(ValueError):
                    self.pack_pair(image(version='v1.0.54'), bad)
                self.assertFalse((self.root / 'out').exists())

    def test_pair_rejects_s3_in_combined_position(self):
        s3 = image(version='v1.0.54', chip=9, tag=b'CROSSPOINT-BOARD-V1:x4pro;')
        with self.assertRaises(ValueError):
            self.pack_pair(s3, s3)
        self.assertFalse((self.root / 'out').exists())

    def test_board_identity_is_unique_and_unmixed(self):
        for index, tag in enumerate((b'CROSSPOINT-BOARD-V1:x4;CROSSPOINT-BOARD-V1:x4pro;',
                    b'CROSSPOINT-BOARD-V1:x4;CROSSPOINT-BOARD-V1:x4;',
                    b'CROSSPOINT-BOARD-V1:x4;CROSSPOINT-BOARD-V1:sticky;')):
            with self.subTest(tag=tag):
                with self.assertRaises(ValueError):
                    self.pack(image(version=f'0.1.{index}', tag=tag))

    def test_second_conflict_preserves_all_preexisting_bytes(self):
        c3 = image(version='v1.0.54')
        s3 = image(version='v1.0.54', chip=9, tag=b'CROSSPOINT-BOARD-V1:x4pro;')
        self.pack_pair(c3, s3)
        folder = self.root / 'out'
        before = {p.relative_to(folder): p.read_bytes() for p in folder.rglob('*') if p.is_file()}
        with self.assertRaises(ValueError):
            self.pack_pair(c3, image(version='v1.0.54', chip=9, tag=b'CROSSPOINT-BOARD-V1:x4pro;', digest=False))
        after = {p.relative_to(folder): p.read_bytes() for p in folder.rglob('*') if p.is_file()}
        self.assertEqual(before, after)

    def test_reject_missing_duplicate_nul_and_long_product_version(self):
        variants = [image(product_tag=False)]
        variants += [image(version=version) for version in
                     ('0.1.0;TENOR-CROSS-VERSION-V1:0.1.0', '0.1.0\0', 'v' + '1' * 64 + '.0.0')]
        for data in variants:
            with self.subTest(sha=hashlib.sha256(data).hexdigest()):
                with self.assertRaises(ValueError):
                    self.pack(data)
                self.assertFalse((self.root / 'out').exists())

    def test_cli_accepts_optional_x4pro_binary(self):
        self.binary.write_bytes(image(version='v1.0.54'))
        second = self.root / 's3.bin'
        second.write_bytes(image(version='v1.0.54', chip=9, tag=b'CROSSPOINT-BOARD-V1:x4pro;'))
        subprocess.run([sys.executable, str(REPO / 'scripts/package_tenor_ota.py'),
                        str(self.binary), str(self.root / 'out'), '--x4pro-binary', str(second)],
                       check=True, capture_output=True, text=True)
        ready = self.root / 'out/firmware/stable.json.ready'
        self.assertEqual(len(json.loads(ready.read_text())['assets']), 2)


if __name__ == '__main__':
    unittest.main()
