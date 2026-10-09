import argparse
import os
from pathlib import Path
import subprocess
import unittest


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
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=True)
source = (args.activity_source or args.repo / 'src/activities/settings/FontDownloadActivity.cpp').read_text()
extracted = body(source, 'void FontDownloadActivity::downloadFamily(')
for signature in ['void FontDownloadActivity::downloadAll(', 'void FontDownloadActivity::updateAll(',
                  'void FontDownloadActivity::downloadSelected(']:
    if signature in source:
        extracted += '\n' + body(source, signature)
if 'bool FontDownloadActivity::installDownloadedPack(' in source:
    extracted += '\n' + body(source, 'bool FontDownloadActivity::installDownloadedPack(')
else:
    extracted += '\nbool FontDownloadActivity::installDownloadedPack(const char* path) { return installFixturePack(path) == FontPackInstaller::Result::OK; }'
if 'void FontDownloadActivity::updateDownloadProgress(' in source:
    extracted += '\n' + body(source, 'void FontDownloadActivity::updateDownloadProgress(')
if 'void FontDownloadActivity::waitForDownloadPaint(' in source:
    extracted += '\n' + body(source, 'void FontDownloadActivity::waitForDownloadPaint(')
(args.out / 'production_activity.inc').write_text(extracted)
test_dir = Path(__file__).resolve().parent
command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-UNDEBUG', str(test_dir / 'activity_fixture.cpp'),
           '-o', str(args.out / 'activity-test')]
if 'STR_FONT_PACK_NO_SPACE' in (args.repo / 'lib/I18n/I18nKeys.h').read_text():
    command += ['-DFONT_DOWNLOAD_PACK_IDS_PRESENT']
for directory in [test_dir / 'stubs', args.repo / 'test/font_pack/stubs', args.repo / 'src', args.repo / 'lib/I18n', args.out,
                  args.repo / 'freeink-sdk/libs/hardware/SDCardManager/include']:
    command += ['-I', str(directory)]
subprocess.run(command, check=True)


class FontDownloadPack(unittest.TestCase):
    def scenario(self, name):
        result = subprocess.run([str(args.out / 'activity-test'), name], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_success(self): self.scenario('success')
    def test_crc(self): self.scenario('crc')
    def test_space(self): self.scenario('space')
    def test_cancel(self): self.scenario('cancel')
    def test_legacy(self): self.scenario('legacy')
    def test_short(self): self.scenario('short')
    def test_network(self): self.scenario('network')
    def test_installer(self): self.scenario('installer')
    def test_legacy_crc(self): self.scenario('legacycrc')
    def test_legacy_cancel(self): self.scenario('legacycancel')
    def test_batch_order(self): self.scenario('batchorder')
    def test_batch_bytes(self): self.scenario('batchbytes')
    def test_batch_crc_continue(self): self.scenario('batchcrc')
    def test_batch_space_continue(self): self.scenario('batchspace')
    def test_batch_network_continue(self): self.scenario('batchnetwork')
    def test_batch_install_continue(self): self.scenario('batchinstall')
    def test_batch_cancel(self): self.scenario('batchcancel')
    def test_batch_update(self): self.scenario('batchupdate')
    def test_batch_power(self): self.scenario('batchpower')

    def test_two_bars_share_one_paint(self):
        render = body(source, 'void FontDownloadActivity::render(')
        downloading = render.split('} else if (state_ == DOWNLOADING) {', 1)[1].split(
            '} else if (state_ == COMPLETE)', 1)[0]
        self.assertEqual(downloading.count('GUI.drawProgressBar('), 2)
        self.assertIn('batchDownloadedBytes_ + fileProgress_', downloading)
        self.assertIn('batchTotalBytes_', downloading)
        self.assertIn('batchFamilyIndex_', downloading)
        self.assertIn('progressRenderGate_.paintDelay(millis())', render)
        self.assertNotIn('requestUpdate', downloading)

    def test_batch_prevents_sleep_between_families(self):
        header = (args.repo / 'src/activities/settings/FontDownloadActivity.h').read_text()
        sleep = body(header, 'bool preventAutoSleep(')
        self.assertIn('if (batchRunning_) return true;', sleep)

    def test_manifest_dispatch(self):
        self.assertTrue('fontdownload::classifyManifestFile' in source, 'manifest entries must use the classifier')
        header = (args.repo / 'src/activities/settings/FontDownloadActivity.h').read_text()
        self.assertIn('https://cross.tenor.vn/firmware/v1.0.56/fonts/fonts.json', header)


unittest.main(argv=['font-download-pack'])
