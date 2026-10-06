"""tenor/ugly on a button device: the network screens and what the device calls itself.

Founder decisions under test (06/10/2026): a device on tenor/ugly with no name of its own is "xau-nhu-cho" on the
network (hotspot, mDNS); a name the user typed still wins; tenor/cross keeps "tenor-cross".
"""
import json
import re
import shutil
import unittest
import zipfile

from ugly_common import Card, entered, ink

# Diary: the edge Up opens the Settings page, its first row is Send file. In the chooser the front Right moves
# down twice to the hotspot.
TO_THE_HOTSPOT = '1000:UP;1800:CONFIRM;3200:RIGHT;3600:RIGHT;4200:CONFIRM;8000:QUIT'
TO_THE_HOTSPOT_CROSS = '1500:UP;1800:RIGHT;2100:RIGHT;2400:RIGHT;2700:RIGHT;2900:RIGHT;3100:CONFIRM;' \
                       '4200:RIGHT;4800:RIGHT;5600:CONFIRM;9500:QUIT'


class UglyNetworkNameTest(unittest.TestCase):
    def hotspot(self, script=TO_THE_HOTSPOT, **kw):
        card = Card(**kw)
        self.addCleanup(card.close)
        log, _ = card.run(script, timeout=60)
        self.assertIn('CrossPointWebServer', entered(log), log[-2000:])
        self.assertIn('Network mode: AP', log, log[-2000:])
        return log

    def test_an_ugly_device_without_a_name_is_xau_nhu_cho(self):
        log = self.hotspot()
        self.assertIn('SSID: xau-nhu-cho\n', log)
        self.assertIn('mDNS started: http://xau-nhu-cho.local/', log)
        self.assertNotIn('tenor-cross', ''.join(re.findall(r'(?:SSID: |mDNS started: )\S+', log)))

    def test_the_name_the_user_typed_still_wins(self):
        log = self.hotspot(deviceName='Sach cua Lan')
        self.assertIn('SSID: Sach-cua-Lan\n', log)
        self.assertIn('mDNS started: http://Sach-cua-Lan.local/', log)

    def test_tenor_cross_keeps_its_name(self):
        log = self.hotspot(TO_THE_HOTSPOT_CROSS, shell=0)
        self.assertIn('SSID: tenor-cross\n', log)
        self.assertIn('mDNS started: http://tenor-cross.local/', log)


def keys(*names, start=1500, gap=700):
    """The keys one after another, `gap` ms apart; 'WAIT:n' waits n ms more. Returns (script part, ms after the last)."""
    t, parts = start, []
    for k in names:
        if k.startswith('WAIT:'):
            t += int(k[5:])
            continue
        parts.append('%d:%s' % (t, k))
        t += gap
    return ';'.join(parts), t


# Diary -> Settings page -> System (seventh row) -> Clock (first question) -> four rows down -> Sync now.
TO_THE_CLOCK_SYNC = ['UP'] + ['RIGHT'] * 6 + ['CONFIRM', 'CONFIRM'] + ['RIGHT'] * 4 + ['CONFIRM']
NOTE_FRAME = re.compile(r'Note frame title="(?P<title>[^"]*)" line="(?P<line>[^"]*)" percent=(?P<percent>-?\d+) '
                        r'total=(?P<ms>\d+)ms heap=\d+')


class UglyNoteTest(unittest.TestCase):
    def test_clock_sync_says_each_step_on_a_note(self):
        card = Card()
        self.addCleanup(card.close)
        # No Wi-Fi yet: the list of networks comes first; the open one joins, then the clock asks the internet.
        script, t = keys(*TO_THE_CLOCK_SYNC, 'WAIT:2500', 'CONFIRM', 'WAIT:3000')
        log, shots = card.run(script + ';%d:QUIT' % (t + 800), [(t, 'clock')], timeout=60)
        self.assertIn('ClockSync', entered(log), log[-2000:])
        # The list of networks writes its own notes first; the clock's carry its title.
        notes = [m.groupdict() for m in NOTE_FRAME.finditer(log) if m['title'] == 'Đồng bộ đồng hồ']
        said = [n['line'] for i, n in enumerate(notes) if i == 0 or n['line'] != notes[i - 1]['line']]
        self.assertEqual(said,
                         ['Đang hỏi giờ trên mạng. Đằng nào mày cũng trễ.', 'Giờ chuẩn rồi. Hết lý do trễ.'], log[-3000:])
        self.assertTrue(all(n['percent'] == '-1' for n in notes), notes)
        page = shots['clock']
        # The margin of the notebook, the sentence in the middle, the hand-drawn status bar; the old bold header is gone.
        self.assertGreater(ink(page, (24, 200, 30, 600)), 300)
        self.assertGreater(ink(page, (40, 300, 500, 500)), 1500)
        self.assertGreater(ink(page, (0, 750, 528, 792)), 150)
        print('clock note frames ms:', [n['ms'] for n in notes])



SERVER_FRAME = re.compile(r'Server frame ap=(?P<ap>\d) total=(?P<ms>\d+)ms heap=\d+')
# The chooser of Send file: Confirm on the first row joins a network; the open network of the simulator is the first.
TO_THE_SERVER = '1000:UP;1800:CONFIRM;3200:CONFIRM;5600:CONFIRM;10000:QUIT'


class UglyServerTest(unittest.TestCase):
    def run_card(self, script, shot_ms):
        card = Card()
        self.addCleanup(card.close)
        log, shots = card.run(script, [(shot_ms, 'page')], timeout=60)
        self.assertIn('CrossPointWebServer', entered(log), log[-2000:])
        return log, shots['page']

    def test_the_hotspot_page_is_written_by_hand_beside_two_exact_codes(self):
        log, page = self.run_card(TO_THE_HOTSPOT, 7000)
        frames = SERVER_FRAME.findall(log)
        self.assertTrue(frames and all(ap == '1' for ap, _ in frames), log[-2500:])
        # Two codes in the left column, the steps beside them, the margin of the notebook, the status bar.
        self.assertGreater(ink(page, (52, 116, 250, 314)), 12000)
        self.assertGreater(ink(page, (52, 354, 250, 552)), 12000)
        self.assertGreater(ink(page, (274, 120, 520, 170)), 400)
        self.assertGreater(ink(page, (24, 600, 30, 700)), 60)
        self.assertGreater(ink(page, (0, 750, 528, 792)), 150)
        print('hotspot frames ms:', [ms for _, ms in frames])

    def test_the_server_page_on_a_network(self):
        log, page = self.run_card(TO_THE_SERVER, 9000)
        frames = SERVER_FRAME.findall(log)
        self.assertTrue(frames and all(ap == '0' for ap, _ in frames), log[-2500:])
        self.assertGreater(ink(page, (165, 220, 363, 560)), 12000)  # the code, in the middle
        self.assertGreater(ink(page, (24, 600, 30, 700)), 60)
        print('server frames ms:', [ms for _, ms in frames])



# Diary -> Settings page -> Device (eighth row) -> Wi-Fi (third question).
TO_THE_WIFI = ['UP'] + ['RIGHT'] * 7 + ['CONFIRM', 'RIGHT', 'RIGHT', 'CONFIRM']


class UglyWifiTest(unittest.TestCase):
    def wifi(self, **env):
        card = Card()
        self.addCleanup(card.close)
        script, t = keys(*TO_THE_WIFI, 'WAIT:2500', 'CONFIRM', 'WAIT:2500')
        log, shots = card.run(script + ';%d:QUIT' % (t + 800), [(t, 'wifi')], timeout=60, **env)
        self.assertIn('WifiSelection', entered(log), log[-2000:])
        notes = [m.groupdict() for m in NOTE_FRAME.finditer(log)]
        self.assertTrue(all(n['title'] == 'Mạng Wi-Fi' for n in notes), notes)
        return [n['line'] for i, n in enumerate(notes) if i == 0 or n['line'] != notes[i - 1]['line']], shots['wifi'], notes

    def test_the_waits_of_wifi_are_notes(self):
        said, _, notes = self.wifi()
        self.assertEqual(said[:1], ['Đang dò sóng. Đứng yên đó.'])
        # An open network completes at once and hands back, so the note says the join and the screen behind follows.
        self.assertEqual(said[-1:], ['Đang nối mạng. Cầu trời mật khẩu đúng.'])
        print('wifi note frames ms:', [n['ms'] for n in notes])

    def test_the_list_and_the_forget_question_are_written_by_hand(self):
        # A saved network with a password is first in the list; the Left key asks to forget it.
        card = Card()
        self.addCleanup(card.close)
        (card.store / 'wifi.json').write_text(json.dumps(
            {'credentials': [{'ssid': 'Local Test Network (fake)', 'password': 'matkhau1'}]}))
        script, t = keys(*TO_THE_WIFI, 'WAIT:2500')
        log, shots = card.run(script + ';%d:LEFT;%d:QUIT' % (t + 600, t + 2000), [(t, 'list'), (t + 1500, 'ask')],
                              timeout=60)
        self.assertRegex(log, r'Wifi list frame rows=3 ')
        self.assertIn('Wifi ask frame forget=1 sel=0', log)
        self.assertGreater(ink(shots['list'], (30, 150, 500, 300)), 1500)  # three rows in pen
        self.assertGreater(ink(shots['list'], (20, 690, 508, 745)), 500)  # the legend as the tip
        self.assertGreater(ink(shots['ask'], (40, 200, 500, 500)), 2000)

    def test_a_failed_join_says_so(self):
        said, _, _ = self.wifi(CROSSPOINT_SIM_WIFI_CONNECT='fail')
        self.assertEqual(said[-1:], ['Nối không được. Gõ sai mật khẩu chứ gì.'])



# Diary -> Settings page -> Reader (fourth row) -> Manage fonts (second question).
TO_THE_FONTS = ['UP'] + ['RIGHT'] * 3 + ['CONFIRM', 'RIGHT', 'CONFIRM']


class UglyFontTest(unittest.TestCase):
    def test_the_family_list_is_written_by_hand(self):
        card = Card()
        self.addCleanup(card.close)
        mock = card.sd.parent / (card.sd.name + '-http')
        mock.mkdir()
        self.addCleanup(shutil.rmtree, mock)
        family = lambda name: {'name': name, 'description': name, 'styles': ['regular'], 'scripts': [],
                               'files': [{'name': name + '_12.cpfont', 'size': 1000, 'crc32': 1}]}
        (mock / 'fonts.json').write_text(json.dumps({'version': 1, 'baseUrl': 'http://127.0.0.1:9/', 'scriptGroups': [],
                                                     'families': [family('Alpha'), family('Beta')]}))
        script, t = keys(*TO_THE_FONTS, 'WAIT:2500', 'CONFIRM', 'WAIT:3000')
        log, shots = card.run(script + ';%d:QUIT' % (t + 800), [(t, 'font-list')], timeout=60,
                              CROSSPOINT_SIM_HTTP_MOCK_ROOT=str(mock))
        after = log[log.index('Entering activity: FontDownload'):]
        self.assertIn('Manifest loaded: 2 families', after, after[-2000:])
        self.assertRegex(after, r'part=rows runs=\d+')
        self.assertGreater(ink(shots['font-list'], (20, 100, 508, 400)), 1500)

    def test_the_font_list_load_and_its_failure_are_notes(self):
        # The font server answers with a list nobody can read: the list is asked for and the ask fails, offline.
        card = Card()
        self.addCleanup(card.close)
        mock = card.sd.parent / (card.sd.name + '-http')
        mock.mkdir()
        self.addCleanup(shutil.rmtree, mock)
        (mock / 'fonts.json').write_text('not a list of fonts')
        script, t = keys(*TO_THE_FONTS, 'WAIT:2500', 'CONFIRM', 'WAIT:4000')
        log, shots = card.run(script + ';%d:QUIT' % (t + 800), [(t, 'fonts')], timeout=60,
                              CROSSPOINT_SIM_HTTP_MOCK_ROOT=str(mock))
        self.assertIn('FontDownload', entered(log), log[-2000:])
        notes = [m.groupdict() for m in NOTE_FRAME.finditer(log) if m['title'] != 'Mạng Wi-Fi']
        said = [n['line'] for i, n in enumerate(notes) if i == 0 or n['line'] != notes[i - 1]['line']]
        self.assertEqual(said, ['Đang tải danh sách font. Gom cho lắm vào.', 'Cài font xịt.'], log[-3000:])
        self.assertGreater(ink(shots['fonts'], (40, 300, 500, 500)), 500)
        print('font note frames ms:', [n['ms'] for n in notes])



# Diary -> Settings page -> Other (tenth row) -> Check for updates (fifth question).
TO_THE_UPDATE = ['UP'] + ['RIGHT'] * 9 + ['CONFIRM'] + ['RIGHT'] * 4 + ['CONFIRM']


class UglyOtaTest(unittest.TestCase):
    def test_checking_and_no_update_are_notes(self):
        # The simulator's update check never installs: it reports no update.
        card = Card()
        self.addCleanup(card.close)
        script, t = keys(*TO_THE_UPDATE, 'WAIT:2500', 'CONFIRM', 'WAIT:3000')
        log, shots = card.run(script + ';%d:QUIT' % (t + 800), [(t, 'update')], timeout=90)
        self.assertIn('OtaUpdate', entered(log), log[-2000:])
        notes = [m.groupdict() for m in NOTE_FRAME.finditer(log) if m['title'] == 'Cập nhật']
        said = [n['line'] for i, n in enumerate(notes) if i == 0 or n['line'] != notes[i - 1]['line']]
        self.assertEqual(said, ['Đang dò bản mới. Ngồi im.', 'Bản mới nhất rồi. Lỗi là ở mày.'], log[-3000:])
        self.assertGreater(ink(shots['update'], (40, 300, 500, 500)), 1000)
        print('update note frames ms:', [n['ms'] for n in notes])



CALIBRE_FRAME = re.compile(r'Calibre frame receiving=(?P<rx>\d) total=(?P<ms>\d+)ms heap=\d+')
# Send file chooser -> second row (Calibre) -> the open network.
TO_CALIBRE = ['UP', 'CONFIRM', 'WAIT:700', 'RIGHT', 'CONFIRM', 'WAIT:2500', 'CONFIRM']
# Diary -> Settings page -> Other -> KOReader sync (first question) -> seventh row, Log in.
TO_KOREADER_LOGIN = ['UP'] + ['RIGHT'] * 9 + ['CONFIRM', 'CONFIRM'] + ['RIGHT'] * 6 + ['CONFIRM']


class UglyCalibreTest(unittest.TestCase):
    def test_calibre_waits_on_a_page_written_by_hand(self):
        card = Card()
        self.addCleanup(card.close)
        script, t = keys(*TO_CALIBRE, 'WAIT:3000')
        log, shots = card.run(script + ';%d:QUIT' % (t + 800), [(t, 'calibre')], timeout=60)
        self.assertIn('CalibreConnect', entered(log), log[-2000:])
        frames = CALIBRE_FRAME.findall(log)
        self.assertTrue(frames, log[-2500:])
        page = shots['calibre']
        self.assertGreater(ink(page, (40, 140, 500, 290)), 2500)  # the four steps in pen
        self.assertGreater(ink(page, (24, 600, 30, 700)), 60)
        print('calibre frames ms:', [ms for _, ms in frames])


def small_epub(path, paragraphs=40):
    """An original three-line EPUB, enough for the reader and its menu."""
    with zipfile.ZipFile(path, 'w') as out:
        out.writestr('mimetype', 'application/epub+zip')
        out.writestr('META-INF/container.xml', '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:'
                     'container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/>'
                     '</rootfiles></container>')
        out.writestr('book.opf', '<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id">'
                     '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Sach thu</dc:title>'
                     '<dc:identifier id="id">ugly-ket-noi</dc:identifier><dc:language>vi</dc:language></metadata>'
                     '<manifest><item id="b" href="b.xhtml" media-type="application/xhtml+xml"/></manifest>'
                     '<spine><itemref idref="b"/></spine></package>')
        out.writestr('b.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>t</title></head><body>' +
                     '<p>Mot dong chu de lat trang.</p>' * paragraphs + '</body></html>')


# The diary opens the book (Read on), Confirm opens the reader menu, the fourth tab (Tools), its first row: sync.
TO_THE_SYNC = ['CONFIRM', 'WAIT:2200', 'CONFIRM', 'DOWN', 'DOWN', 'DOWN', 'CONFIRM']


class UglyKoreaderTest(unittest.TestCase):
    def test_a_progress_sync_that_fails_is_a_note(self):
        card = Card(books=[('Sach thu', 'sach.epub')])
        self.addCleanup(card.close)
        small_epub(card.sd / 'sach.epub')
        (card.store / 'koreader.json').write_text(json.dumps(
            {'cfgVersion': 3, 'username': 'lan', 'password': 'x', 'serverUrl': 'http://127.0.0.1:9'}))
        (card.store / 'wifi.json').write_text(json.dumps({'credentials': [{'ssid': 'Simulator WiFi (fake)'}],
                                                         'lastConnectedSsid': 'Simulator WiFi (fake)'}))
        script, t = keys(*TO_THE_SYNC, 'WAIT:5000')
        log, shots = card.run(script + ';%d:QUIT' % (t + 800), [(t, 'kosync')], timeout=60)
        self.assertIn('KOReaderSync', entered(log), log[-2500:])
        notes = [m.groupdict() for m in NOTE_FRAME.finditer(log) if m['title'] == 'Đồng bộ KOReader']
        self.assertTrue(notes, log[-2500:])
        self.assertEqual(notes[-1]['line'], 'Đồng bộ xịt. Lý do ghi dưới kia.', notes)

    def test_a_koreader_login_that_fails_is_a_note(self):
        card = Card()
        self.addCleanup(card.close)
        # A name and a password, and a server that never answers: the log in fails offline.
        (card.store / 'koreader.json').write_text(json.dumps(
            {'cfgVersion': 3, 'username': 'lan', 'password': 'x', 'serverUrl': 'http://127.0.0.1:9'}))
        script, t = keys(*TO_KOREADER_LOGIN, 'WAIT:2500', 'CONFIRM', 'WAIT:4000')
        log, shots = card.run(script + ';%d:QUIT' % (t + 800), [(t, 'koreader')], timeout=60)
        self.assertIn('KOReaderAuth', entered(log), log[-2000:])
        notes = [m.groupdict() for m in NOTE_FRAME.finditer(log) if m['title'] != 'Mạng Wi-Fi']
        self.assertTrue(notes, log[-2500:])
        self.assertEqual(notes[-1]['line'], 'Đăng nhập xịt. Lý do ghi dưới kia, đọc đi.', notes)
        print('koreader note frames ms:', [n['ms'] for n in notes])



BLE_TIP = re.compile(r'BLE tip="(?P<tip>[^"]*)" status="(?P<status>[^"]*)"')
# Diary -> Settings page -> Device (eighth row) -> Bluetooth page turner (fourth question).
TO_THE_BLE = ['UP'] + ['RIGHT'] * 7 + ['CONFIRM'] + ['RIGHT'] * 3 + ['CONFIRM']


class UglyBleTest(unittest.TestCase):
    def test_a_status_too_long_for_its_row_goes_to_the_tip(self):
        # The simulator has no Bluetooth: the long status leaves the row for the tip, in the voice of the shell.
        card = Card()
        self.addCleanup(card.close)
        script, t = keys(*TO_THE_BLE, 'WAIT:1000')
        log, shots = card.run(script + ';%d:QUIT' % (t + 800), [(t, 'ble')], timeout=60)
        self.assertIn('BlePageTurner', entered(log), log[-2000:])
        tips = BLE_TIP.findall(log)
        self.assertTrue(tips, log[-2500:])
        self.assertEqual(tips[-1], ('Bản này đâu có Bluetooth. Lật bằng tay đi.', ''))
        self.assertGreater(ink(shots['ble'], (20, 680, 508, 745)), 500)  # the tip, written above the key bar


if __name__ == '__main__':
    unittest.main()
