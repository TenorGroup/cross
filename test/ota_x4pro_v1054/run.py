#!/usr/bin/env python3
"""Run the production OTA updater and manifest parser for C3 and S3 with fake flash."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

cli = argparse.ArgumentParser(description=__doc__)
cli.add_argument('--output', type=Path, required=True)
cli.add_argument('--cxx', default='c++')
cli.add_argument('--case', help='Run case names containing this text')
args = cli.parse_args()
here = Path(__file__).resolve().parent
repo = here.parents[1]
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
spec = importlib.util.spec_from_file_location('package_fixture', repo / 'test/ota_package/test_package.py')
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
results = []
for board, chip, macro in (('x3-x4', 5, 'FREEINK_DEVICE_X3'), ('x4pro', 9, 'FREEINK_DEVICE_X4PRO')):
    binary = out / board
    flags = ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-deprecated-declarations',
             '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-DCROSSPOINT_EMULATED=0',
             f'-D{macro}=1', '-DCROSSPOINT_VERSION="v1.0.53"']
    includes = [here / 'stubs', repo / 'lib/JsonParser', repo / 'test/ota_back_cancel/stubs',
                repo / 'test/file_transfer_back_latch/stubs', repo / 'src/network', repo / 'lib/NetworkTrust']
    sources = [here / 'OtaBoards.cpp', repo / 'src/network/OtaUpdater.cpp',
               repo / 'src/network/FirmwareBoardTag.cpp', repo / 'src/network/TlsRecordSlot.cpp',
               repo / 'lib/JsonParser/ReleaseJsonParser.cpp', repo / 'lib/JsonParser/StreamingJsonParser.cpp']
    subprocess.run([args.cxx, *flags, *['-I' + str(p) for p in includes], *map(str, sources),
                    *([] if sys.platform == 'darwin' else ['-lcrypto']), '-o', str(binary)], check=True)
    board_tag = b'CROSSPOINT-BOARD-V1:' + (b'x4' if chip == 5 else b'x4pro') + b';'
    good = bytes(fixture.image(version='v1.0.54', chip=chip, tag=board_tag))
    url = f'https://cross.tenor.vn/firmware/v1.0.54/tenor-cross-v1.0.54-{board}.bin'
    base_asset = {'name': f'tenor-cross-{board}.bin', 'browser_download_url': url,
                  'size': len(good), 'digest': 'sha256:' + hashlib.sha256(good).hexdigest()}
    other = 'x4pro' if chip == 5 else 'x3-x4'
    other_asset = dict(base_asset, name=f'tenor-cross-{other}.bin', browser_download_url=url + '.other')
    cases = []
    def case(name, data=good, asset=None, tag='v1.0.54', check=0, install=0, activate=1, dry=0, assets=None):
        chosen = dict(base_asset, size=len(data), digest='sha256:' + hashlib.sha256(data).hexdigest())
        if asset:
            chosen.update(asset)
        document = {'tag_name': tag, 'assets': assets if assets is not None else [other_asset, chosen]}
        cases.append((name, data, document, check, install, activate, dry))
    case('two-assets')
    case('two-assets-reversed', assets=[base_asset, other_asset])
    case('missing-asset', assets=[other_asset], check=1, activate=0)
    case('duplicate-asset', assets=[base_asset, base_asset], check=3, activate=0)
    case('invalid-offer-version', tag='v1.0.54-rc.1', check=3, activate=0)
    case('older-offer', tag='v1.0.53', install=4, activate=0)
    case('tiny-size', asset={'size': 23}, check=3, activate=0)
    case('oversized', asset={'size': 0x640001}, install=5, activate=0)
    case('body-short', asset={'size': len(good) + 1}, install=8, activate=0)
    case('body-long', asset={'size': len(good) - 1}, install=8, activate=0)
    case('wrong-digest', asset={'digest': 'sha256:' + '0' * 64}, install=8, activate=0)
    case('wrong-chip', data=bytes(fixture.image(version='v1.0.54', chip=5 if chip == 9 else 9, tag=board_tag)),
         install=7, activate=0)
    case('wrong-board', data=bytes(fixture.image(version='v1.0.54', chip=chip,
                                                tag=b'CROSSPOINT-BOARD-V1:sticky;')), install=7, activate=0)
    for name, version in (('wrong-image-version', 'v1.0.53'), ('nul-version', 'v1.0.54\0'),
                          ('overflow-version', 'v1.0.54' + 'x' * 32),
                          ('duplicate-version', 'v1.0.54;TENOR-CROSS-VERSION-V1:v1.0.54')):
        case(name, data=bytes(fixture.image(version=version, chip=chip, tag=board_tag)), install=8, activate=0)
    # End the stream immediately before the version's semicolon; esp_ota_end must stay untouched.
    case('unterminated-version', data=good[:320 + len(b'TENOR-CROSS-VERSION-V1:') + len('v1.0.54')],
         install=8, activate=0)
    case('missing-version', data=bytes(fixture.image(version='v1.0.54', chip=chip, tag=board_tag,
                                                   product_tag=False)), install=8, activate=0)
    case('dry-run', dry=1, activate=0)
    case('write-failure', install=5, activate=0)
    case('end-failure', install=5, activate=0)
    for name, data, document, check, install, activate, dry in cases:
        if args.case and args.case not in name:
            continue
        folder = out / f'{board}-{name}'
        folder.mkdir(exist_ok=True)
        (folder / 'image.bin').write_bytes(data)
        (folder / 'manifest.json').write_text(json.dumps(document))
        (folder / 'url.txt').write_text(url)
        for chunk in (1, 7, 64, 1024):
            run = subprocess.run([str(binary), str(folder), str(chunk), str(check), str(install),
                                  str(activate), str(dry)], capture_output=True, text=True)
            row = {'board': board, 'case': name, 'chunk': chunk, 'exit': run.returncode,
                   'log': run.stdout + run.stderr}
            results.append(row)
            print(f'{board} {name} chunk={chunk}: {row["log"]}', end='')
(out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
print(f'{sum(row["exit"] == 0 for row in results)}/{len(results)} passed')
raise SystemExit(int(any(row['exit'] for row in results)))
