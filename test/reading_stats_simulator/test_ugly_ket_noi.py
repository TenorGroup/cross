"""tenor/ugly on a button device: the network screens and what the device calls itself.

Founder decisions under test (06/10/2026): a device on tenor/ugly with no name of its own is "xau-nhu-cho" on the
network (hotspot, mDNS); a name the user typed still wins; tenor/cross keeps "tenor-cross".
"""
import re
import unittest

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

    def test_a_failed_join_says_so(self):
        said, _, _ = self.wifi(CROSSPOINT_SIM_WIFI_CONNECT='fail')
        self.assertEqual(said[-1:], ['Nối không được. Gõ sai mật khẩu chứ gì.'])


if __name__ == '__main__':
    unittest.main()
