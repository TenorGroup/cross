"""Read actual packed font variants through the X3 simulator."""
import json, os, shutil, subprocess, sys, tempfile, unittest
from pathlib import Path
import test_rework_font_boundaries as boundaries
REPO=boundaries.REPO

class FontWeightTest(unittest.TestCase):
    run_sim = boundaries.ReworkFontBoundariesTest.run_sim
    READING = boundaries.ReworkFontBoundariesTest.READING

    @classmethod
    def setUpClass(cls):
        cls.fixture = tempfile.TemporaryDirectory(prefix='weight-pack-')
        cls.addClassCleanup(cls.fixture.cleanup)
        cls.pack = Path(cls.fixture.name)/'Trial'
        subprocess.run([sys.executable,str(REPO/'lib/EpdFont/scripts/fontconvert_sdcard.py'),
            '--regular',str(REPO/'lib/EpdFont/builtinFonts/source/NotoSans/NotoSans-Regular.ttf'),
            '--name','Trial','--intervals','ascii','--trial-weights','--output-dir',str(cls.pack)],
            check=True,capture_output=True,text=True)

    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='weight-sim-')
        self.addCleanup(self.temp.cleanup)
        self.sd=Path(self.temp.name);self.store=self.sd/'.crosspoint';self.store.mkdir()
        (self.sd/'books').mkdir()
        shutil.copyfile(REPO/'test/epubs/test_kerning_ligature.epub',self.sd/'books/sach.epub')
        (self.store/'recent.json').write_text(json.dumps({'books':[{'path':'/books/sach.epub','title':'Trial'}]}))
        shutil.copytree(self.pack,self.sd/'.fonts/Trial')
        self.settings=dict(language='VI',fontSize=26,sdFontFamilyName='Trial',readerInkWeight=2,sleepTimeout=10)

    def test_installed_26_and_persistence(self):
        saved,log=self.run_sim('1000:CONFIRM;1700:CONFIRM;8500:QUIT')
        self.assertEqual(saved['fontSize'],26)
        self.assertEqual(saved['readerInkWeight'],2)
        self.assertIn('/weight-2/Trial_26.cpfont',log)
        self.assertIn('Entering activity: EpubReader',log)
        self.settings=saved
        saved,log=self.run_sim('1000:CONFIRM;1700:CONFIRM;8500:QUIT')
        self.assertEqual(saved['readerInkWeight'],2)

    def test_missing_variant_preserves_family(self):
        (self.sd/'.fonts/Trial/weight-2/Trial_26.cpfont').unlink()
        saved,log=self.run_sim('1000:CONFIRM;1700:CONFIRM;8500:QUIT')
        self.assertEqual((saved['sdFontFamilyName'],saved['fontSize'],saved['readerInkWeight']),('Trial',26,0))

    def test_corrupt_variant_preserves_family(self):
        (self.sd/'.fonts/Trial/weight-2/Trial_26.cpfont').write_bytes(b'broken')
        saved,log=self.run_sim('1000:CONFIRM;1700:CONFIRM;8500:QUIT')
        self.assertEqual((saved['sdFontFamilyName'],saved['readerInkWeight']),('Trial',0))

    def test_weight_change_returns_to_book(self):
        self.settings.update(fontSize=16,readerInkWeight=0)
        script=self.READING+';11300:RIGHT;12000:RIGHT;12700:CONFIRM;15000:DOWN;16000:RIGHT;16700:RIGHT;17400:RIGHT;18100:RIGHT;19000:CONFIRM;22000:BACK;26000:QUIT'
        saved,log=self.run_sim(script)
        self.assertEqual(saved['readerInkWeight'],1)
        self.assertIn('/weight-1/Trial_16.cpfont',log)
        self.assertIn('Entering activity: TextSettings',log)

    def test_26_from_popup(self):
        self.settings.update(fontSize=14,readerInkWeight=0)
        saved,log=self.run_sim(self.READING+';11500:CONFIRM;13200:LEFT;13900:LEFT;14600:CONFIRM;19000:QUIT')
        self.assertEqual(saved['fontSize'],26)

if __name__=='__main__': unittest.main()
