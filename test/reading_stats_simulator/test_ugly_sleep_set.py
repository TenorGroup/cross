"""The tenor/ugly sleep set in the simulator: 8 doodles that follow the day and the sleep count, the same
frame twice for the same day, a greeting on the first diary frame after a start, and the speed of going to sleep.

The simulator names the day it wants through CROSSPOINT_SIM_UGLY_SLEEP (day,count,hour,minutes,percent,night).
Set UGLY_SHOTS to a directory to keep the screenshots as PNG.
"""
import re
import unittest

from ugly_common import Card, digest, ink

DOODLE = (0, 320, 528, 792)  # where a doodle lives
TOP = (0, 0, 528, 32)  # where the greeting of the diary is written, above the title of the diary


def sleep(day, count=0, hour=14, name='z', **extra):
    card = Card(sleepScreen=11, **extra)
    try:
        log, shots = card.run('1500:SLEEP;6000:QUIT', [(5000, name)], CROSSPOINT_SIM_UGLY_SLEEP='%s,%d,%d' % (day, count, hour))
    finally:
        card.close()
    return log, shots[name]


class UglySleepSetTest(unittest.TestCase):
    def test_three_days_three_doodles(self):
        images = [sleep(day)[1] for day in (20261001, 20261002, 20261003)]
        self.assertEqual(len({digest(i) for i in images}), 3)
        for i in images:
            self.assertGreater(ink(i, DOODLE), 1200, 'a doodle')
            self.assertGreater(ink(i, (0, 80, 528, 320)), 500, 'a sentence of handwriting')

    def test_the_same_day_draws_the_same_frame_twice(self):
        a = sleep(20261004, 3, 13)
        b = sleep(20261004, 3, 13)
        self.assertEqual(digest(a[1]), digest(b[1]))
        self.assertIn('[UGLY] sleep ready=1', a[0])

    def test_eight_sleeps_in_a_day_draw_eight_doodles(self):
        images = [sleep(20261004, n, 13, name='ngu%d' % n)[1] for n in range(8)]
        self.assertEqual(len({digest(i) for i in images}), 8)
        self.assertEqual(len({digest(i.crop(DOODLE)) for i in images}), 8, 'the doodles differ, not only the sentences')

    def test_thirty_sleeps_at_22_never_repeat_a_sentence(self):
        # The complaint of 06/10: the 22-24 band held 1 line and showed it in 20 of 30 sleeps. The count is
        # pumped here, since the simulator loses it on every wake (the X3 keeps it in RTC).
        from multiprocessing.pool import ThreadPool

        def sentence(n):
            card = Card(sleepScreen=11)
            try:
                _, shots = card.run('1500:SLEEP;4500:QUIT', [(3500, 's')], CROSSPOINT_SIM_UGLY_SLEEP='20261006,%d,22,20,0' % n)
            finally:
                card.close()
            return digest(shots['s'].crop((0, 80, 528, 320)))

        with ThreadPool(4) as pool:
            seen = pool.map(sentence, range(100, 130))
        self.assertEqual(sum(a == b for a, b in zip(seen, seen[1:])), 0, 'the same sentence twice in a row')
        self.assertLessEqual(max(seen.count(s) for s in seen), 6, 'one sentence in over a fifth of 30 sleeps')

    def test_going_to_sleep_stays_quick(self):
        log, _ = sleep(20261004)
        visible = int(re.search(r'sleep ready=1 visible=(\d+) ms', log).group(1))
        self.assertLessEqual(visible, 60, 'the simulator draws the frame in a few ms')

    def test_first_diary_frame_after_a_wake_greets_by_the_hour_and_then_goes(self):
        from PIL import Image
        card = Card(sleepScreen=11)
        self.addCleanup(card.close)
        env = dict(CROSSPOINT_SIM_UGLY_SLEEP='20261004,,11')
        awake = card.sd / 'awake.bmp'
        card.run('1500:SLEEP;6000:POWER', CROSSPOINT_SIM_INPUT_SCRIPT_AFTER_WAKE='4000:QUIT',
                 CROSSPOINT_SIM_SCREENSHOTS_AFTER_WAKE='2500:%s' % awake, **env)
        greeted = Image.open(awake).convert('1')
        self.assertGreater(ink(greeted, TOP), 150, 'the greeting is written above the diary')
        self.assertEqual(ink(greeted, (0, 70, 528, 79)), 0, 'the title of the diary steps down under the greeting')
        # A plain start is not a wake: the same hour, no greeting.
        _, shots = card.run('4000:QUIT', [(2000, 'b')], CROSSPOINT_SIM_UGLY_SLEEP='20261004,,11')
        self.assertLess(ink(shots['b'], TOP), 20)


if __name__ == '__main__':
    unittest.main()
