import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def function(text, name):
    start = text.index('function ' + name + '(')
    opening = text.index('{', start)
    depth = 1
    cursor = opening + 1
    while depth:
        depth += (text[cursor] == '{') - (text[cursor] == '}')
        cursor += 1
    return text[start:cursor]


class FontPackEntrypoints(unittest.TestCase):
    def test_browser_accepts_pack_and_preserves_full_family_name(self):
        html = (ROOT / 'src/network/html/FontsPage.html').read_text()
        code = function(html, 'familyFromFilename') + '\n' + function(html, 'cpfontFilesOnly')
        code += '''
const assert = require('assert');
assert.strictEqual(familyFromFilename('NotoSerif.cpfontpack'), 'NotoSerif');
assert.strictEqual(familyFromFilename('Example_Family.cpfontpack'), 'Example_Family');
assert.strictEqual(familyFromFilename('Example_14.cpfont'), 'Example');
assert.deepStrictEqual(cpfontFilesOnly([{name:'A.cpfont'},{name:'A.cpfontpack'},{name:'A.ttf'}]).map(file=>file.name),
                       ['A.cpfont','A.cpfontpack']);
'''
        result = subprocess.run(['node', '-e', code], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_boot_scans_after_storage_mount_and_before_font_loading(self):
        main = (ROOT / 'src/main.cpp').read_text()
        mounted = main.index('if (!Storage.begin())')
        scan = main.index('FontPackInstaller::scan()', mounted)
        loaded = main.index('APP_STATE.loadFromFile()', mounted)
        self.assertLess(scan, loaded)

    def test_uploads_use_owner_callback_and_pin_pack_manifest_url(self):
        server = (ROOT / 'src/network/CrossPointWebServer.cpp').read_text()
        activity = (ROOT / 'src/activities/network/CrossPointWebServerActivity.cpp').read_text()
        self.assertTrue('fontPackApplier' in server)
        self.assertGreaterEqual(server.count('prepareFontPackUpload('), 3)
        self.assertGreaterEqual(server.count('applyFontPackUpload('), 4)
        owner = activity.split('setFontPackApplier(', 1)[1].split('});', 1)[0]
        self.assertTrue('RenderLock lock(*this)' in owner)
        self.assertTrue('FontPackInstaller::scan()' in owner)
        header = (ROOT / 'src/activities/settings/FontDownloadActivity.h').read_text()
        self.assertTrue('https://cross.tenor.vn/firmware/v1.0.56/fonts/fonts.json' in header)
        self.assertTrue('installDownloadedPack' in header)


if __name__ == '__main__':
    unittest.main()
