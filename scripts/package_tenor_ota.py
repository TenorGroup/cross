#!/usr/bin/env python3
"""Package a verified ESP32-C3 application for hosting on cross.tenor.vn."""
import argparse
import hashlib
import json
import re
import shutil
import struct
from pathlib import Path


def validate_image(data):
    """Check the unsigned ESP image framing before producing its OTA digest."""
    if len(data) < 80 or len(data) > 0x640000 or data[0] != 0xE9:
        raise ValueError('Invalid application image or partition size')
    if not 1 <= data[1] <= 16 or data[23] not in (0, 1):
        raise ValueError('Invalid segment count or image digest flag')
    offset = 24
    checksum = 0xEF
    for index in range(data[1]):
        if offset + 8 > len(data):
            raise ValueError('Truncated segment header')
        _, size = struct.unpack_from('<II', data, offset)
        offset += 8
        if size > len(data) - offset:
            raise ValueError('Truncated segment data')
        if index == 0 and (size < 256 or data[offset:offset + 4] != b'\x32\x54\xcd\xab'):
            raise ValueError('Missing application descriptor')
        for byte in memoryview(data)[offset:offset + size]:
            checksum ^= byte
        offset += size
    checksum_offset = offset | 15
    if checksum_offset >= len(data) or data[checksum_offset] != checksum:
        raise ValueError('Invalid image checksum')
    image_end = checksum_offset + 1
    expected_end = image_end + (32 if data[23] else 0)
    if len(data) != expected_end:
        raise ValueError('Truncated image digest or unexpected trailing data')
    if data[23] and hashlib.sha256(data[:image_end]).digest() != data[image_end:]:
        raise ValueError('Invalid image SHA-256')


def package(binary, output):
    data = binary.read_bytes()
    validate_image(data)
    if int.from_bytes(data[12:14], 'little') != 5:
        raise ValueError('Expected ESP32-C3 application')
    if b'CROSSPOINT-BOARD-V1:x4;' not in data:
        raise ValueError('Missing combined X3/X4 board tag')
    marker = b'TENOR-CROSS-VERSION-V1:'
    if data.count(marker) != 1:
        raise ValueError('Missing or ambiguous tenor/cross product version tag')
    start = data.index(marker) + len(marker)
    end = data.find(b';', start, start + 64)
    if end < 0:
        raise ValueError('Unterminated product version tag')
    version = data[start:end].decode('ascii')
    if not re.fullmatch(r'v?(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)', version):
        raise ValueError('Use a stable tenor/cross product version')
    if any(int(part) > 99999 for part in version.lstrip('v').split('.')):
        raise ValueError('Release version exceeds firmware version parser limits')
    sha = hashlib.sha256(data).hexdigest()
    folder = output / 'firmware' / version
    folder.mkdir(parents=True, exist_ok=True)
    name = f'tenor-cross-{version}-x3-x4.bin'
    target = folder / name
    if target.exists() and hashlib.sha256(target.read_bytes()).hexdigest() != sha:
        raise ValueError('Version already contains a different binary; use a new version')
    shutil.copyfile(binary, target)
    (folder / (name + '.sha256')).write_text(f'{sha}  {name}\n')
    manifest = {'tag_name': version, 'assets': [{
        'name': 'tenor-cross-x3-x4.bin',
        'browser_download_url': f'https://cross.tenor.vn/firmware/{version}/{name}',
        'size': len(data), 'digest': 'sha256:' + sha,
    }]}
    # The operator publishes the version directory first, then atomically replaces stable.json.
    staging = output / 'firmware' / 'stable.json.ready'
    staging.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))
    return manifest


if __name__ == '__main__':
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('binary', type=Path)
    cli.add_argument('output', type=Path)
    args = cli.parse_args()
    package(args.binary, args.output)
