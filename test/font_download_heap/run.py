import argparse
import os
from pathlib import Path
import subprocess


def body(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    depth = 1
    cursor = opening + 1
    while depth:
        depth += (text[cursor] == '{') - (text[cursor] == '}')
        cursor += 1
    return text[start:cursor]


parser = argparse.ArgumentParser()
parser.add_argument('--repo', type=Path, required=True)
parser.add_argument('--out', type=Path, required=True)
parser.add_argument('--activity-source', type=Path)
parser.add_argument('--manager-source', type=Path)
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=True)
source = (args.activity_source or args.repo / 'src/activities/settings/FontDownloadActivity.cpp').read_text()
methods = '\n'.join(body(source, signature) for signature in [
    'void FontDownloadActivity::clearManifest(', 'bool FontDownloadActivity::internString(',
    'bool FontDownloadActivity::fetchAndParseManifest('])
methods = methods.replace('JsonDocument doc;', 'JsonDocument doc(&jsonAllocator);')
methods = methods.replace('JsonDocument filter;', 'JsonDocument filter(&jsonAllocator);')
signature = 'bool FontDownloadActivity::prepareDownloadHeap('
methods += '\n' + (body(source, signature) if signature in source else
                   'bool FontDownloadActivity::prepareDownloadHeap() { return true; }')
(args.out / 'production.inc').write_text(methods)
manager = (args.manager_source or args.repo / 'lib/EpdFont/SdCardFontManager.cpp').read_text()
signature = 'void SdCardFontManager::releaseReaderForDownload('
(args.out / 'manager.inc').write_text(body(manager, signature) if signature in manager else
                                    'void SdCardFontManager::releaseReaderForDownload(GfxRenderer&) {}')
test_dir = Path(__file__).resolve().parent
command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-UNDEBUG', '-Wall', '-Wextra',
           str(test_dir / 'heap.cpp'), '-o', str(args.out / 'heap-test')]
for directory in [args.out, args.repo / 'test/huge_book', args.repo / 'lib/Memory',
                  args.repo / '.pio/libdeps/gh_release/ArduinoJson/src',
                  args.repo / 'freeink-sdk/libs/ui/FreeInkUI/include',
                  args.repo / 'src/activities/settings']:
    command += ['-I', str(directory)]
subprocess.run(command, check=True)
subprocess.run([str(args.out / 'heap-test')], check=True)
