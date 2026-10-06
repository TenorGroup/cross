"""tenor/ugly on the button readers, the screens outside the shared parts (v1.0.52).

Each scenario runs the X3 simulator in both shells: tenor/ugly says it in its own voice, tenor/cross keeps its words
and its pixels. The answer paper, the notices of a wake and of a quiet restart, the boot screen, the keyboard, the
screen after a crash, Clear cache, the update from the card.
"""
import json
import os
from pathlib import Path
import re
import tempfile
import unittest

import glass_model
from ugly_common import Card, digest, entered, ink

WAKE_NOTICE = re.compile(r'Wake notice shown: (.*)')
RESTART_NOTICE = re.compile(r'Restart notice: (.*)')
SSID = 'Mang Mau'
UGLY_BOOT = re.compile(r'\[UGLY\] boot visible=(\d+) ms')


def settings_route(group, question, extra=()):
    """From the diary to a question of the answer sheets: Settings page, group, Select, question, Select."""
    return ['UP'] + ['RIGHT'] * group + ['CONFIRM'] + ['RIGHT'] * (question - 1) + ['CONFIRM'] + list(extra)


def script(keys, start=1000, gap=700, settle=1800):
    """`keys` as an input script; returns (script, ms of the shot after the last key)."""
    parts, t = [], start
    for key in keys:
        if key.startswith('WAIT:'):
            t += int(key[5:])
            continue
        parts.append('%d:%s' % (t, key))
        t += gap
    shot = t - gap + settle
    parts.append('%d:QUIT' % (shot + 600))
    return ';'.join(parts), shot


class UglyRemainingScreensTest(unittest.TestCase):
    def card(self, **kw):
        card = Card(**kw)
        self.addCleanup(card.close)
        return card

    def shot(self, shell, keys, box=None, prep=None, env=None, **kw):
        """The screen after `keys`, in a shell; the activities entered; the log."""
        card = self.card(shell=shell, **kw)
        if prep:
            prep(card)
        run, at = script(keys)
        log, shots = card.run(run, [(at, 'shot')], **(env or {}))
        image = shots['shot']
        return (image.crop(box) if box else image), entered(log), log

    def assert_hand_only_in_ugly(self, keys, box, activity, **kw):
        """tenor/ugly writes the box by hand; tenor/cross draws what it always drew. Same steps, same card."""
        ugly, ugly_acts, ugly_log = self.shot(1, keys, box, **kw)
        cross, cross_acts, cross_log = self.shot(0, keys, box, **kw)
        self.assertIn(activity, ugly_acts, ugly_log[-2000:])
        self.assertIn(activity, cross_acts, cross_log[-2000:])
        self.assertGreater(ink(ugly), 200, 'the box has writing')
        self.assertNotEqual(digest(ugly), digest(cross), 'tenor/ugly draws the box in its own hand')
        again, _, _ = self.shot(1, keys, box, **kw)
        self.assertEqual(digest(ugly), digest(again), 'the hand is deterministic')
        return ugly, cross

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

    def test_the_boot_screen_is_the_dog_on_the_books(self):
        # A cold start: tenor/ugly boots on its own doodle in black and white, tenor/cross on its brand art.
        temp = tempfile.TemporaryDirectory(prefix='cross-ugly-boot-')
        self.addCleanup(temp.cleanup)
        tool = glass_model.build(Path(temp.name) / 'tool')
        frames = {}
        for shell in (1, 0):
            card = self.card(shell=shell)
            trace = card.sd / 'panel.trace'
            log, _ = card.run('2500:QUIT', CROSSPOINT_SIM_PANEL_TRACE=str(trace))
            self.assertEqual(entered(log)[:1], ['Boot'], log[-1500:])
            if shell:
                self.assertEqual(len(UGLY_BOOT.findall(log)), 1, log[-1500:])
                self.assertNotIn('[BRAND] boot', log)
            else:
                self.assertIn('[BRAND] boot', log)
                self.assertEqual(UGLY_BOOT.findall(log), [])
            first = next(r['i'] for r in glass_model.replay(tool, trace) if r['op'] == 'display')
            pgm = Path(temp.name) / ('boot%d.pgm' % shell)
            glass_model.replay(tool, trace, dumps=[(first, pgm)])
            frames[shell] = pgm.read_bytes()
        self.assertNotEqual(frames[1], frames[0])
        keep = Path(os.environ.get('UGLY_SHOTS', '') or temp.name)
        keep.mkdir(parents=True, exist_ok=True)
        (keep / 'boot_ugly.pgm').write_bytes(frames[1])

    def test_the_keys_are_written_by_hand(self):
        # Device name (Device, question 2) opens the keyboard: the keys in hand, the circle on the key under the cursor.
        self.assert_hand_only_in_ugly(settings_route(7, 2), (0, 300, 528, 580), 'KeyboardEntry')

    def test_the_screen_after_a_crash_speaks_in_the_voice_of_the_shell(self):
        reason = {'CROSSPOINT_SIM_PANIC': 'Guru Meditation Error: Core 0 panic (LoadProhibited) PC 0x42001234'}
        self.assert_hand_only_in_ugly([], (0, 60, 528, 460), 'Crash', env=reason)

    def test_clear_cache_says_it_in_hand(self):
        # Other, question 4: the question box, the pen on Clear, Select; the result stays on the screen.
        self.assert_hand_only_in_ugly(settings_route(9, 4, ['RIGHT', 'CONFIRM', 'WAIT:1500']), (0, 300, 528, 520),
                                      'ClearCache')

    def test_the_update_from_the_card_says_it_in_hand(self):
        # Other, question 6 opens the picker; a file that is no firmware fails the check and the screen says so.
        def junk(card):
            (card.sd / 'fw.bin').write_bytes(b'not a firmware' * 64)
        self.assert_hand_only_in_ugly(settings_route(9, 6, ['CONFIRM', 'WAIT:1500']), (0, 300, 528, 520),
                                      'SdFirmwareUpdate', prep=junk)


if __name__ == '__main__':
    unittest.main()
