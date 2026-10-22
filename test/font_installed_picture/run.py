# The installed list of Tải font, from the production source: loadInstalledList() and rebuildStoreRowItems() are
# cut out of FontDownloadActivity.cpp (and the struct out of its header) and compiled against a scripted card.
import argparse
from pathlib import Path
import re
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
parser.add_argument('--source', type=Path)
parser.add_argument('--outdated-off', action='store_true')
parser.add_argument('--own-off', action='store_true')  # the switch, flipped, for the check of the other side  # a mutated copy of the activity, for the mutation check
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=True)
source = (args.source or args.repo / 'src/activities/settings/FontDownloadActivity.cpp').read_text()
header = (args.repo / 'src/activities/settings/FontDownloadActivity.h').read_text()
if args.own_off: source = source.replace('constexpr bool kOwnFontNames = true;', 'constexpr bool kOwnFontNames = false;')
if args.outdated_off: source = source.replace('constexpr bool kPictureForOutdatedPack = true;', 'constexpr bool kPictureForOutdatedPack = false;')
(args.out / 'struct.inc').write_text(body(header, 'struct InstalledFont') + ';\n')
helpers = ''
if 'kPictureForOutdatedPack' not in source: helpers += 'constexpr bool kPictureForOutdatedPack = true;  // the source has no switch yet\n'
for found in re.finditer(r'^constexpr [\w:]+ (?:kPictureForOutdatedPack|kOwnFontNames|kBlankWidth|kBlank)\b.*;$', source, re.M):
    helpers += found.group(0) + '\n'
for signature in ['bool inStoreCatalog(', 'std::string displayName(', 'const fui::LabelImage* storePicture(',
                  'const fui::LabelImage* ownNamePicture(']:
    if signature in source: helpers += body(source, signature) + '\n'
(args.out / 'helpers.inc').write_text(helpers)
(args.out / 'methods.inc').write_text('\n'.join(body(source, signature) for signature in [
    'void FontDownloadActivity::loadInstalledList(', 'void FontDownloadActivity::rebuildStoreRowItems(',
    'const SdCardFontFamilyInfo* FontDownloadActivity::ownNameFamily(']) + '\n')
test_dir = Path(__file__).resolve().parent
command = ['c++', '-std=c++20', '-UNDEBUG', '-Wall', '-Wextra', str(test_dir / 'fixture.cpp'), '-o', str(args.out / 'fixture')]
for directory in [args.repo / 'test/font_download_pack/stubs', args.repo / 'test/font_pack/stubs', args.repo / 'src',
                  args.repo / 'lib/I18n', args.out, args.repo / 'freeink-sdk/libs/hardware/SDCardManager/include',
                  args.repo / 'lib/EpdFont', args.repo / 'freeink-sdk/libs/ui/FreeInkUI/include']:
    command += ['-I', str(directory)]
if args.own_off: command += ['-DOWN_OFF']
if args.outdated_off: command += ['-DOUTDATED_OFF']
subprocess.run(command, check=True)
raise SystemExit(subprocess.run([str(args.out / 'fixture')]).returncode)
