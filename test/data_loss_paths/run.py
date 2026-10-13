#!/usr/bin/env python3
"""Run extracted production deletion methods on lane-local FAT image files."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(os.path.realpath(__file__)).parents[2]
HERE = Path(os.path.realpath(__file__)).parent


def read(ref, path):
    repo = ROOT
    if path.startswith('freeink-sdk/'):
        path = path[len('freeink-sdk/'):]
        sdk_ref = subprocess.check_output(['git', 'ls-tree', ref, 'freeink-sdk'], cwd=ROOT, text=True).split()[2]
        repo = ROOT / 'freeink-sdk'
        ref = sdk_ref
    return subprocess.check_output(['git', 'show', ref + ':' + path], cwd=repo, text=True)


def method(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == '{': depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0: return text[start:i + 1]
    raise ValueError(signature)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ref')
    ap.add_argument('--build', required=True, type=Path)
    ap.add_argument('--sdfat', required=True, type=Path)
    ap.add_argument('--strict', action='store_true')
    ap.add_argument('--guard-probe', action='store_true')
    args = ap.parse_args()
    build = Path(os.path.realpath(args.build))
    build.mkdir(parents=True, exist_ok=True)
    inputs = {}
    def get(path):
        text = read(args.ref, path)
        inputs[path] = hashlib.sha256(text.encode()).hexdigest()
        return text
    try:
        deletion = method(get('src/activities/home/FileActions.cpp'), 'bool remove(')
    except subprocess.CalledProcessError:
        deletion = method(get('src/activities/home/FileBrowserActivity.cpp'), 'bool FileBrowserActivity::removeDirFile(')
        deletion = deletion.replace('bool FileBrowserActivity::removeDirFile(', 'bool remove(')
        deletion = deletion.replace('bool remove(const std::string& fullPath) {',
                                    'bool remove(const std::string& fullPath) {\n  auto fileNameBuffer = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);')
        deletion = deletion.replace('  if (!fileNameBuffer) {\n    LOG_ERR("FileBrowser", "fileNameBuffer not allocated");\n    return false;\n  }\n\n', '')
    sdk = method(get('freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp'), 'bool SDCardManager::removeDir(')
    dav = get('src/network/WebDAVHandler.cpp')
    normalize = method(get('lib/FsHelpers/FsHelpers.cpp'), 'std::string normalisePath(')
    # Sensitivity probe lives only in generated host code, never in product source.
    if args.guard_probe:
        deletion = deletion.replace('  auto file =', '  if (FsHelpers::normalisePath(fullPath).empty()) return false;\n  auto file =', 1)
        sdk = sdk.replace('  auto dir =', '  if (FsHelpers::normalisePath(path).empty()) return false;\n  auto dir =', 1)
    methods = [deletion, sdk] + [method(dav, signature) for signature in (
        'String WebDAVHandler::getRequestPath(', 'bool WebDAVHandler::isProtectedPath(', 'void WebDAVHandler::handleDelete(')]
    (build/'policy.h').write_text(get('src/network/WebPathPolicy.h'))
    generated = (HERE/'card.cpp.in').read_text().replace('@NORMALIZE@', normalize).replace('@METHODS@', '\n'.join(methods))
    (build/'card.cpp').write_text(generated)
    (build/'hashes.json').write_text(json.dumps(inputs, indent=2)+'\n')
    dep=Path(os.path.realpath(args.sdfat))
    command=['c++','-std=c++20','-O3','-w','-DENABLE_ARDUINO_FEATURES=0','-DENABLE_ARDUINO_SERIAL=0',
             '-DENABLE_ARDUINO_STRING=0','-DUSE_BLOCK_DEVICE_INTERFACE=1','-DSPI_DRIVER_SELECT=3',
             '-DUSE_SEPARATE_FAT_CACHE=0','-DSDFAT_EXPECT_SEPARATE_FAT_CACHE=0','-include',str(ROOT/'test/sdfat_alloc/host_prelude.h'),
             '-I'+str(dep),str(build/'card.cpp')]
    command += [str(p) for folder in ('FatLib','common') for p in sorted((dep/folder).glob('*.cpp'))]
    command += ['-o',str(build/'card-test')]
    (build/'command.json').write_text(json.dumps(command,indent=2)+'\n')
    subprocess.run(command,check=True,timeout=120)
    result=subprocess.run([str(build/'card-test'),str(build/'card.img'),'strict' if args.strict else 'observe'],
                          text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=120)
    (build/'result.txt').write_text(result.stdout)
    print(result.stdout)
    (build/'card.img').unlink(missing_ok=True)
    return result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
