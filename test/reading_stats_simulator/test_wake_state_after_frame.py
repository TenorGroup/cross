"""v1.0.13: a wake to Home writes nothing to the card before its first frame is up.

The sleep path leaves showBootScreen false in state.json and the frame the panel keeps in
sleep_frame.bin. A wake by the power button skips the splash once, then re-arms it. The re-arm used
to be written before any painting, on a card that had just been powered again (about 340 ms on the
X3), and the frame file was deleted as soon as it was read (another write). Now:
1. the re-arm is written after the first Home frame, and the frame file stays for the next sleep,
   which always rewrites or removes it;
2. a wake cut off before its first frame (power lost, or a hang) is repeated as it was: still no
   splash, from the same frame;
3. any boot that shows the splash re-arms a flag it finds cleared, so a crash reboot does not leave
   the next power-on to skip the splash;
4. a sleep pressed while the first frame is still on its way still ends with the flag cleared.
The panel's slow refresh is played by CROSSPOINT_SIM_REFRESH_MS.
"""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
FRAME_BYTES = 528 * 792 // 8


class WakeStateAfterFrameTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='cross-wake-state-')
        self.addCleanup(temp.cleanup)
        self.sd = Path(temp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        (self.store / 'settings.json').write_text(json.dumps({'language': 'EN', 'sleepTimeout': 120}))

    def asleep(self):
        """The card as the sleep path leaves it."""
        (self.store / 'state.json').write_text(json.dumps({'showBootScreen': False}))
        (self.store / 'sleep_frame.bin').write_bytes(b'\xff' * FRAME_BYTES)

    def run_sim(self, script, wake, refresh_ms=0):
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_INPUT_SCRIPT=script)
        if wake:
            env['CROSSPOINT_SIM_WAKE_REASON'] = 'power'
        if refresh_ms:
            env['CROSSPOINT_SIM_REFRESH_MS'] = str(refresh_ms)
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=60)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-4000:])
        return log

    def flag(self):
        return json.loads((self.store / 'state.json').read_text()).get('showBootScreen', True)

    def frame_kept(self):
        return (self.store / 'sleep_frame.bin').exists()

    def test_first_frame_up_rearms_the_splash_and_keeps_the_frame(self):
        self.asleep()
        log = self.run_sim('3000:QUIT', wake=True)
        self.assertIn('Restored sleep frame baseline', log)
        self.assertNotIn('Entering activity: Boot', log)
        self.assertIs(self.flag(), True, log[-3000:])
        self.assertTrue(self.frame_kept(), 'the wake deleted the frame file')

    def test_wake_cut_before_its_first_frame_is_repeated(self):
        self.asleep()
        # The first frame takes 5 s; the power goes after 1.5 s.
        log = self.run_sim('1500:QUIT', wake=True, refresh_ms=5000)
        self.assertIn('Entering activity: Home', log)
        self.assertIs(self.flag(), False, 'the splash was re-armed before the first frame was up')
        self.assertTrue(self.frame_kept())
        log = self.run_sim('3000:QUIT', wake=True)
        self.assertIn('Restored sleep frame baseline', log)
        self.assertNotIn('Entering activity: Boot', log)
        self.assertIs(self.flag(), True)

    def test_a_boot_with_the_splash_rearms_a_cleared_flag(self):
        self.asleep()
        # Not a power-button wake (a crash reboot, a flash): the splash shows and the flag is set.
        log = self.run_sim('3000:QUIT', wake=False)
        self.assertIn('Entering activity: Boot', log)
        self.assertIs(self.flag(), True, log[-3000:])
        # So the next power-button start is an ordinary one, with the splash.
        log = self.run_sim('3000:QUIT', wake=True)
        self.assertIn('Entering activity: Boot', log)
        self.assertNotIn('Restored sleep frame baseline', log)

    def test_sleep_pressed_during_the_first_frame_leaves_the_flag_cleared(self):
        self.asleep()
        log = self.run_sim('400:SLEEP;15000:QUIT', wake=True, refresh_ms=1500)
        self.assertIn('Entering deep sleep', log)
        self.assertIs(self.flag(), False, log[-3000:])


if __name__ == '__main__':
    unittest.main()
