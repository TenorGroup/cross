"""tenor/ugly on the button readers, the screens outside the shared parts (v1.0.52).

Each scenario runs the X3 simulator in both shells: tenor/ugly says it in its own voice, tenor/cross keeps its words
and its pixels. The answer paper, the notices of a wake and of a quiet restart, the boot screen, the keyboard, the
screen after a crash, Clear cache, the update from the card.
"""
import json
import re
import unittest

from ugly_common import Card, entered

WAKE_NOTICE = re.compile(r'Wake notice shown: (.*)')
RESTART_NOTICE = re.compile(r'Restart notice: (.*)')
SSID = 'Mang Mau'
UGLY_BOOT = re.compile(r'\[UGLY\] boot visible=(\d+) ms')


class UglyRemainingScreensTest(unittest.TestCase):
    def card(self, **kw):
        card = Card(**kw)
        self.addCleanup(card.close)
        return card

    def test_the_wake_notice_speaks_in_the_voice_of_the_shell(self):
        # "Wake-up notice" on, a sleep folded to black and white: the notice goes over the kept sleep frame.
        for shell, language, said in ((1, 'VI', 'Đang dụi mắt, từ từ.'), (1, 'EN', 'Rubbing my eyes. Hold on.'),
                                      (0, 'VI', 'Đang khởi động'), (0, 'EN', 'Starting up')):
            card = self.card(shell=shell, language=language, wakeNotice=1, sleepBwFold=1)
            (card.store / 'state.json').write_text(json.dumps({'showBootScreen': False}))
            log, _ = card.run('3000:SLEEP;6000:POWER;12000:QUIT', CROSSPOINT_SIM_WAKE_REASON='power',
                              CROSSPOINT_SIM_INPUT_SCRIPT_AFTER_WAKE='2500:QUIT')
            self.assertEqual(WAKE_NOTICE.findall(log), [said], (shell, language, log[-1500:]))

    def test_the_quiet_restart_after_file_transfer_speaks_in_the_voice_of_the_shell(self):
        # File transfer on a saved network, then Back: the device restarts quietly behind a notice.
        routes = {1: '1000:UP;1800:CONFIRM;3200:CONFIRM;8000:BACK:80;12000:QUIT',
                  0: '1500:UP;1800:RIGHT;2100:RIGHT;2400:RIGHT;2700:RIGHT;2900:RIGHT;3100:CONFIRM;4400:CONFIRM;'
                     '9000:BACK:80;13000:QUIT'}
        for shell, language, said in ((1, 'VI', 'Đợi tí, đang lục đồ.'), (1, 'EN', 'Hang on, digging through my stuff.'),
                                      (0, 'VI', 'Đang tải')):
            card = self.card(shell=shell, language=language)
            (card.store / 'wifi.json').write_text(json.dumps({'lastConnectedSsid': SSID, 'credentials': [{'ssid': SSID}]}))
            log, _ = card.run(routes[shell])
            self.assertIn('CrossPointWebServer', entered(log), (shell, log[-2500:]))
            self.assertEqual(RESTART_NOTICE.findall(log), [said], (shell, language, log[-2500:]))


if __name__ == '__main__':
    unittest.main()
