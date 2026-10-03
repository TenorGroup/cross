"""What the X3 UC8279 glass shows after a sleep and a wake, or after a cold start (v1.0.17).

The UC8279 refreshes only the pixels where the new frame differs from the controller's previous
frame (DTM1). A pixel whose previous frame says what the new frame says is not driven, so whatever
the glass holds there stays. Each journey here runs in the simulator with its panel calls written
to a trace, and the trace is replayed through the SDK's UC8279 driver over a model of the
controller RAM and the glass (glass_model.py, glass/uc8279_glass.cpp, rules at the top of that
file). Once the device has started again, every B/W frame it shows must be on the glass pixel for
pixel: a pixel left over is the ghost of the sleep screen or of the boot picture.

Journeys (the fixtures turn "Black and white refresh before sleep", key sleepBwFold, on unless a
journey sets it off; it is off by default since v1.0.52): the Tenor sleep screen folded to black and white, the same without heap
for the kept plane, the gray sleep images with "Black and white refresh before sleep" off (Tenor,
a gray picture of the user's own, a book cover), the transparent sleep screen with the switch on
and off, a sleep frame cut short on the card, Quick resume, and a cold start after a sleep with the Tenor
boot picture and with the classic boot screen.
"""

import hashlib
import json
from pathlib import Path
import tempfile
import unittest

import glass_model
from cai_dat_truoc_tenor import truoc_tenor
from test_sleep_ends_bw import BOOK, gray_bmp, write_epub

SLEEP_THEN_WAKE = '3000:SLEEP;6000:POWER;12000:QUIT'
AFTER_WAKE = '2500:QUIT'


def tree_hash(root):
    """Names and contents of every file under `root`: what a wake that gave up must leave as it was."""
    digest = hashlib.sha256()
    for path in sorted(Path(root).rglob('*')):
        if path.is_file() and path.name != 'panel.trace':
            digest.update(str(path.relative_to(root)).encode())
            digest.update(path.read_bytes())
    return digest.hexdigest()


def gray_picture(sd):
    (sd / 'sleep.bmp').write_bytes(gray_bmp(528, 792))


def overlay_picture(sd):
    (sd / 'sleep-overlay.bmp').write_bytes(gray_bmp(200, 200))


def book(sd):
    write_epub(sd / BOOK.lstrip('/'))


class WakeGlassTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='cross-wake-glass-')
        cls.tool = glass_model.build(Path(cls.temp.name) / 'tool')

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def journey(self, name, settings, pre=None, extra=None, state=None, cold=False):
        """Start awake on a known glass, sleep, then wake by the power button (or start cold).
        Returns the replayed panel calls and the simulator log."""
        sd = Path(self.temp.name) / name
        store = sd / '.crosspoint'
        store.mkdir(parents=True)
        (store / 'settings.json').write_text(json.dumps(truoc_tenor(dict({'language': 'EN', 'sleepBwFold': 1}, **settings))))
        (store / 'state.json').write_text(json.dumps(dict({'showBootScreen': False}, **(state or {}))))
        # The first process wakes onto a white glass whose frame it knows, so every pixel it leaves
        # comes from the journey itself.
        (store / 'sleep_frame.bin').write_bytes(b'\xff' * glass_model.PANEL_BYTES)
        if pre:
            pre(sd)
        trace = sd / 'panel.trace'
        if cold:
            code, log = glass_model.run_simulator(sd, trace, '3000:SLEEP;6000:QUIT', wake='power', extra=extra)
            self.assertEqual(code, 0, log)
            code, second = glass_model.run_simulator(sd, trace, '3000:QUIT')
            log += second
        else:
            code, log = glass_model.run_simulator(sd, trace, SLEEP_THEN_WAKE, AFTER_WAKE, wake='power', extra=extra)
        self.assertEqual(code, 0, log)
        self.assertIn('Entering deep sleep', log)
        records = glass_model.replay(self.tool, trace)
        self.assertEqual([r for r in records if 'anomaly' in r], [])
        self.assertEqual(sum(r['op'] == 'begin' for r in records), 2, log)
        return records, log

    def assert_glass_follows_every_frame_after_start(self, records):
        start = max(r['i'] for r in records if r['op'] == 'begin')
        shown = [r for r in records if r['i'] > start and r['op'] == 'display']
        self.assertTrue(shown)
        left = [(r['i'], r['glass_vs_frame']) for r in shown if r['glass_vs_frame']]
        self.assertEqual(left, [], 'pixels left on the glass after (record, count)')

    def test_folded_tenor_sleep_wakes_from_its_kept_frame(self):
        records, log = self.journey('tenor', {'sleepScreen': 8})
        self.assert_glass_follows_every_frame_after_start(records)
        # The kept frame names every pixel: the wake refreshes only those that change.
        wake = log.split('Entering deep sleep', 1)[1]
        self.assertIn('Restored sleep frame baseline', wake)
        self.assertNotIn('Redrive every pixel', wake)

    def test_folded_tenor_sleep_without_heap_for_the_kept_plane(self):
        records, _ = self.journey('tenor-no-heap', {'sleepScreen': 8}, extra={'CROSSPOINT_SIM_SLEEP_NO_HEAP': '1'})
        self.assert_glass_follows_every_frame_after_start(records)

    def test_gray_sleep_images_with_the_switch_off(self):
        for name, settings, pre, state in (
                ('tenor-gray', {'sleepScreen': 8}, None, None),
                ('picture-gray', {'sleepScreen': 2}, gray_picture, None),
                ('cover-gray', {'sleepScreen': 3}, book, {'openEpubPath': BOOK})):
            with self.subTest(name):
                records, log = self.journey(name, dict(settings, sleepBwFold=0), pre, state=state)
                self.assertIn('displayGrayBuffer', log.split('Entering deep sleep', 1)[0])
                self.assert_glass_follows_every_frame_after_start(records)

    def test_wake_without_a_kept_frame(self):
        records, log = self.journey('frame-cut-short', {'sleepScreen': 8},
                                    extra={'CROSSPOINT_SIM_SHORT_WRITE_FILE': 'sleep_frame.bin'})
        self.assertNotIn('Restored sleep frame baseline', log.split('Entering deep sleep', 1)[1])
        self.assert_glass_follows_every_frame_after_start(records)

    def test_transparent_sleep_screen(self):
        for name, switch in (('transparent', 1), ('transparent-gray', 0)):
            with self.subTest(name):
                records, _ = self.journey(name, {'sleepScreen': 7, 'sleepBwFold': switch}, overlay_picture)
                self.assert_glass_follows_every_frame_after_start(records)

    def test_quick_resume(self):
        records, _ = self.journey('quick-resume', {'sleepScreen': 6})
        self.assert_glass_follows_every_frame_after_start(records)

    def test_a_wake_that_is_not_held_leaves_glass_and_card_alone(self):
        # A short press wakes the chip and the boot goes on while the hold is watched: the kept frame
        # goes back into the controller, but nothing reaches the glass and nothing the card.
        for name, when, ops in (('early', 'early', []), ('final', 'final', ['begin', 'cleanup', 'deep_sleep'])):
            with self.subTest(name):
                sd = Path(self.temp.name) / ('short-' + name)
                store = sd / '.crosspoint'
                store.mkdir(parents=True)
                (store / 'settings.json').write_text(json.dumps(truoc_tenor({'language': 'EN', 'sleepScreen': 8})))
                (store / 'state.json').write_text(json.dumps({'showBootScreen': False}))
                (store / 'sleep_frame.bin').write_bytes(b'\xff' * glass_model.PANEL_BYTES)
                trace = sd / 'panel.trace'
                code, log = glass_model.run_simulator(sd, trace, '3000:SLEEP;6000:QUIT')
                self.assertEqual(code, 0, log)
                self.assertIn('Entering deep sleep', log)
                before = tree_hash(sd)
                asleep = len(glass_model.replay(self.tool, trace))
                code, wake = glass_model.run_simulator(sd, trace, '2500:QUIT', wake='power',
                                                       extra={'CROSSPOINT_SIM_WAKE_RELEASED': when})
                self.assertEqual(code, 0, wake)
                self.assertIn('Power-button wake not held', wake)
                self.assertNotIn('Entering activity', wake)
                self.assertEqual('Wake frame' in wake, when == 'final')
                self.assertEqual(tree_hash(sd), before)
                records = glass_model.replay(self.tool, trace)[asleep:]
                self.assertEqual([r['op'] for r in records], ops)
                self.assertEqual([r for r in records if 'anomaly' in r], [])

    def test_a_tap_wakes_when_the_quick_press_is_sleep(self):
        # With the quick press on Sleep a tap locks the device, so a tap also wakes it: no hold needed.
        records, log = self.journey('tap-wakes', {'sleepScreen': 8, 'shortPwrBtn': 1},
                                    extra={'CROSSPOINT_SIM_WAKE_RELEASED': 'final'})
        wake = log.split('Entering deep sleep', 1)[1]
        self.assertNotIn('not held', wake)
        self.assertIn('Entering activity: Home', wake)
        self.assert_glass_follows_every_frame_after_start(records)

    def test_cold_start_after_a_sleep(self):
        records, log = self.journey('cold-tenor', {'sleepScreen': 8}, cold=True)
        self.assertIn('Entering activity: Boot', log.split('Entering deep sleep', 1)[1])
        self.assert_glass_follows_every_frame_after_start(records)


if __name__ == '__main__':
    unittest.main()
