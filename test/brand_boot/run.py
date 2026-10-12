"""Compile the shared production renderer and check its native plane uploads."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
import zlib

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--baseline', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    renderer_cpp = ROOT / 'src/components/X3BrandScreen.cpp'
    source = renderer_cpp.read_text()
    if args.baseline:
        source = subprocess.check_output(['git', '-C', str(ROOT), 'show', '8ccfb49e:src/components/X3BrandScreen.cpp']).decode()
        renderer_cpp = args.output/'baseline.cpp'
        renderer_cpp.write_text(source)
        print('Baseline 8ccfb49e renderer SHA256', hashlib.sha256(source.encode()).hexdigest(), flush=True)
    name = 'renderBrandScreen' if 'bool renderBrandScreen(' in source else 'renderX3BrandScreen'
    cmd = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O1', '-ffunction-sections',
           '-fsanitize=address,undefined', '-DCROSSPOINT_VERSION="v1.0.57"',
           '-DFREEINK_DEVICE_X4PRO=1', '-DRENDER_BRAND=' + name]
    cmd += ['-Wl,-dead_strip'] if sys.platform == 'darwin' else ['-Wl,--gc-sections']
    includes = [HERE/'stubs', ROOT/'test/inflate_stream/stubs', ROOT/'src',
                ROOT/'lib/miniz/src', ROOT/'lib/Memory', ROOT/'lib/hal',
                ROOT/'lib/uzlib/src', ROOT/'lib/InflateReader', ROOT/'src/components']
    cmd += ['-I'+str(path) for path in includes]
    files = [HERE/'BrandBoot.cpp', renderer_cpp,
             ROOT/'lib/miniz/src/InflateStream.cpp', ROOT/'lib/miniz/src/miniz_impl.c',
             ROOT/'lib/Memory/BuildScratch.cpp', ROOT/'lib/hal/HalMemory.cpp',
             HERE/'DecoderAlloc.cpp', ROOT/'lib/uzlib/src/tinflate.c']
    binary = args.output/'brand-boot'
    objects = []
    for path in files:
        if path.suffix == '.c':
            obj = args.output/(path.stem+'.o')
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-O1', '-ffunction-sections', '-fsanitize=address,undefined'] + ['-I'+str(p) for p in includes] + ['-c', str(path), '-o', str(obj)], check=True)
            objects.append(obj)
    files = [p for p in files if p.suffix != '.c'] + objects
    subprocess.run(cmd + list(map(str, files)) + ['-o', str(binary)], check=True)
    cases = ['x4', 'x4-combined', 'x3', 'no-gray', 'wrong-size', 'wrong-orientation']
    for case in cases + ([] if args.baseline else ['x4-sleep', 'x3-sleep']):
        subprocess.run([str(binary), case, str(args.output)], check=True)
    if not args.baseline:
        x3_binary = args.output/'brand-boot-x3'
        x3_cmd = [arg.replace('-DFREEINK_DEVICE_X4PRO=1', '-DFREEINK_DEVICE_X4PRO=0') for arg in cmd]
        subprocess.run(x3_cmd + list(map(str, files)) + ['-o', str(x3_binary)], check=True)
        for case in ['x3', 'x4-disabled']:
            subprocess.run([str(x3_binary), case, str(args.output)], check=True)
    shown = [('x3','X3BrandAssets.h','BOOT'), ('x4','X4ProBrandAssets.h','BOOT')]
    if not args.baseline:
        shown += [('x3-sleep','X3BrandAssets.h','SLEEP'), ('x4-sleep','X4ProBrandAssets.h','SLEEP')]
    for device, asset, art in shown:
        header = (ROOT/'src/components'/asset).read_text()
        for plane in ['LSB','MSB']:
            data = re.search(r'\b'+art+'_'+plane+r'\[\] = \{(.*?)\};', header, re.S).group(1)
            packed = bytes(int(v,16) for v in re.findall(r'0x([0-9a-f]{2})',data))
            uploaded = (args.output/(device+'-'+plane.lower()+'.bin')).read_bytes()
            assert uploaded == zlib.decompress(packed), (device, plane)
    for lang in ['vietnamese','english']:
        text = (ROOT/f'lib/I18n/translations/{lang}.yaml').read_text()
        boot = re.search(r'^STR_BOOTING: "(.*)"',text,re.M).group(1)
        assert not boot.isupper(), (lang, boot)
    assert hashlib.sha256((ROOT/'src/components/X3BrandAssets.h').read_bytes()).hexdigest() == 'e48399e1cb21ed1d35781b94c552de19e7fa6f6662a1b5a752f552075fdccbd4'
    assert hashlib.sha256((ROOT/'src/components/ManNguTenor.h').read_bytes()).hexdigest() == '1e614ede8397d91eadf1b7d4b39d7da086d51f21758f2955fc85fe00b0ad2b18'
    boot = (ROOT/'src/activities/boot_sleep/BootActivity.cpp').read_text()
    assert 'if (renderBrandScreen(renderer, true)) return;' in boot
    print('GREEN: X4 Pro/X3 uploads, combined/separate gray, fallback, orientation, boot strings')


if __name__ == '__main__':
    main()
