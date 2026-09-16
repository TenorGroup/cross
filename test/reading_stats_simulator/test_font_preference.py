"""Real simulator regressions for weight preference across font changes."""
from pathlib import Path
import json, os, shutil, subprocess, tempfile, unittest, sys
REPO=Path(__file__).resolve().parents[2]
PROGRAM=Path(os.environ.get('TEST_PROGRAM', REPO/'.pio/build/simulator_x3_uc8279/program'))
class PreferenceTest(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.fixture=tempfile.TemporaryDirectory(prefix='preference-font-')
  cls.addClassCleanup(cls.fixture.cleanup)
  cls.pack=Path(cls.fixture.name)/'Bokerlam'
  # Synthetic family label preserves picker order; glyphs are the OFL Noto Sans fixture.
  subprocess.run([sys.executable,str(REPO/'lib/EpdFont/scripts/fontconvert_sdcard.py'),
   '--regular',str(REPO/'lib/EpdFont/builtinFonts/source/NotoSans/NotoSans-Regular.ttf'),
   '--name','Bokerlam','--sizes','16','--intervals','ascii','--trial-weights',
   '--output-dir',str(cls.pack)],check=True,capture_output=True,text=True)

 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory(prefix='font-pref-');self.addCleanup(self.tmp.cleanup)
  self.sd=Path(self.tmp.name);self.store=self.sd/'.crosspoint';self.store.mkdir()
  for fam,weights in [('Bokerlam',[0,1,2]),('ABaseOnly',[0]),('CWeighted',[0,1,2])]:
   for w in weights:
    sub=Path(f'weight-{w}') if w else Path('')
    dst=self.sd/'.fonts'/fam/sub/f'{fam}_16.cpfont';dst.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(self.pack/sub/'Bokerlam_16.cpfont',dst)
  self.settings=dict(language='VI',uiTheme=4,sdFontFamilyName='Bokerlam',fontSize=16,readerInkWeight=2,sleepTimeout=10)
  (self.store/'menu-customization.json').write_text(json.dumps(dict(version=1,tabs=dict(home=[0,1,4,2,3],settings=list(range(7)),reader=list(range(4)),text=list(range(4))),pins=['text/fontFamily'])))
 def run_sim(self, actions):
  (self.store/'settings.json').write_text(json.dumps(self.settings))
  env={k:v for k,v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
  env.update(SDL_VIDEODRIVER='dummy',CROSSPOINT_SIM_SD=str(self.sd),CROSSPOINT_SIM_INPUT_SCRIPT=actions)
  run=subprocess.run([str(PROGRAM)],cwd=REPO,env=env,capture_output=True,text=True,timeout=20)
  self.assertEqual(run.returncode,0,run.stdout+run.stderr)
  self.settings=json.loads((self.store/'settings.json').read_text())
  return run.stdout+run.stderr
 def roundtrip(self, steps):
  log=self.run_sim('800:DOWN;1300:DOWN;1800:CONFIRM;'+steps+';6200:QUIT')
  self.assertEqual(self.settings['sdFontFamilyName'],'Bokerlam')
  self.assertEqual(self.settings['readerInkWeight'],2)
  self.assertEqual(log.count('Loaded /.fonts/Bokerlam/weight-2/Bokerlam_16.cpfont'),2,log)
 def test_base_only_roundtrip(self):self.roundtrip('3000:LEFT;3500:CONFIRM;4200:RIGHT;4700:CONFIRM')
 def test_weighted_roundtrip(self):self.roundtrip('3000:RIGHT;3500:CONFIRM;4200:LEFT;4700:CONFIRM')
 def test_builtin_roundtrip(self):self.roundtrip('2700:LEFT;3000:LEFT;3500:CONFIRM;4000:RIGHT;4300:RIGHT;4700:CONFIRM')
 def test_corrupt_variant_does_not_retry_each_preview(self):
  (self.sd/'.fonts/Bokerlam/weight-2/Bokerlam_16.cpfont').write_bytes(b'broken')
  log=self.run_sim('800:DOWN;1300:DOWN;1800:CONFIRM;3000:CONFIRM;4200:CONFIRM;5500:QUIT')
  self.assertEqual(self.settings['readerInkWeight'],2)
  self.assertEqual(log.count('Loaded /.fonts/Bokerlam/Bokerlam_16.cpfont'),1,log)
  (self.sd/'.fonts/Bokerlam/weight-2/Bokerlam_16.cpfont').write_bytes((self.pack/'weight-2/Bokerlam_16.cpfont').read_bytes())
  log=self.run_sim('2500:QUIT')
  self.assertIn('Loaded /.fonts/Bokerlam/weight-2/Bokerlam_16.cpfont',log)
if __name__=='__main__':unittest.main()
