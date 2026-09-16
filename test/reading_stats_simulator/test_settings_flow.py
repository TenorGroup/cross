"""Verify wake menu persistence and one-Back return to the selected Home group."""
import json, os, subprocess, tempfile, unittest
from pathlib import Path
REPO = Path(__file__).resolve().parents[2]
class SettingsFlowTest(unittest.TestCase):
    def test_wake_choice_persists_and_back_keeps_home_group(self):
        with tempfile.TemporaryDirectory(prefix='cross-settings-flow-') as tmp:
            sd=Path(tmp); store=sd/'.crosspoint'; store.mkdir()
            (store/'settings.json').write_text(json.dumps({'language':'VI','wakeButtons':0}))
            env={k:v for k,v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
            env.update(SDL_VIDEODRIVER='dummy',CROSSPOINT_SIM_SD=tmp,
                CROSSPOINT_SIM_INPUT_SCRIPT='1000:UP;1800:CONFIRM;2600:RIGHT;3300:RIGHT;4000:RIGHT;4700:CONFIRM;5700:CONFIRM;6500:RIGHT;7300:RIGHT;8100:RIGHT;8900:CONFIRM;9800:BACK;10800:CONFIRM;11800:CONFIRM;12800:BACK;13800:BACK;14800:QUIT')
            run=subprocess.run([str(REPO/'.pio/build/simulator_x3_uc8279/program')],cwd=REPO,env=env,capture_output=True,text=True,timeout=22)
            log=run.stdout+run.stderr
            self.assertEqual(run.returncode,0,log)
            self.assertEqual(json.loads((store/'settings.json').read_text())['wakeButtons'],3,log)
            self.assertEqual(log.count('Entering activity: Settings'),2,log)
            self.assertEqual(log.count('Popped from activity stack, new size = 0'),2,log)
            env['CROSSPOINT_SIM_INPUT_SCRIPT']='1600:QUIT'
            run=subprocess.run([str(REPO/'.pio/build/simulator_x3_uc8279/program')],cwd=REPO,env=env,capture_output=True,text=True,timeout=6)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            self.assertEqual(json.loads((store/'settings.json').read_text())['wakeButtons'],3)
