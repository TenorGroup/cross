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
        notes = [m.groupdict() for m in NOTE_FRAME.finditer(log)]
        said = [n['line'] for i, n in enumerate(notes) if i == 0 or n['line'] != notes[i - 1]['line']]
        self.assertEqual(said,
                         ['Đang hỏi giờ trên mạng. Đằng nào mày cũng trễ.', 'Giờ chuẩn rồi. Hết lý do trễ.'], log[-3000:])
        self.assertTrue(all(n['title'] == 'Đồng bộ đồng hồ' and n['percent'] == '-1' for n in notes), notes)
        page = shots['clock']
        # The margin of the notebook, the sentence in the middle, the hand-drawn status bar; the old bold header is gone.
        self.assertGreater(ink(page, (24, 200, 30, 600)), 300)
        self.assertGreater(ink(page, (40, 300, 500, 500)), 1500)
        self.assertGreater(ink(page, (0, 750, 528, 792)), 150)
        print('clock note frames ms:', [n['ms'] for n in notes])


if __name__ == '__main__':
    unittest.main()
