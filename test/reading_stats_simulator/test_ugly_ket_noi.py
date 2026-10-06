"""tenor/ugly on a button device: the network screens and what the device calls itself.

Founder decisions under test (06/10/2026): a device on tenor/ugly with no name of its own is "xau-nhu-cho" on the
network (hotspot, mDNS); a name the user typed still wins; tenor/cross keeps "tenor-cross".
"""
import re
import unittest

from ugly_common import Card, entered

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


if __name__ == '__main__':
    unittest.main()
