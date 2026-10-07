"""tenor/ugly on the X3: a device already in the shell when this build arrives shows its hidden clock, once.

A settings file saved before the build carries no uiShellClockOnce; in tenor/ugly such a file with the header clock hidden
shows the time. With the flag set, a hidden clock stays hidden. A clock in tenor/cross is left alone.
(The X4 Pro twin, with the saved file, is x4pro_simulator/test_ugly_gio_san.py.)
"""
import unittest

from ugly_common import Card, ink

CLOCK = (450, 765, 528, 792)  # the right end of the diary's status bar


class UglyClockOnceTest(unittest.TestCase):
    def clock_ink(self, shell, **settings):
        card = Card(shell=shell, **settings)
        self.addCleanup(card.close)
        _, shots = card.run('3000:QUIT', [(2000, 'd')])
        return ink(shots['d'], CLOCK)

    def test_a_legacy_hidden_clock_shows_in_ugly_once(self):
        legacy = self.clock_ink(1, uiShellClockOnce=0, clockShowInHeader=0, clockShowHeader=0)
        hidden = self.clock_ink(1, uiShellClockOnce=1, clockShowInHeader=0, clockShowHeader=0)
        self.assertGreater(legacy, hidden + 40, (legacy, hidden))

    def test_cross_keeps_its_hidden_clock(self):
        legacy = self.clock_ink(0, uiShellClockOnce=0, clockShowHeader=0)
        hidden = self.clock_ink(0, uiShellClockOnce=1, clockShowHeader=0)
        self.assertEqual(legacy, hidden)


if __name__ == '__main__':
    unittest.main()
