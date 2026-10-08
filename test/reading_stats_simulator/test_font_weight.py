"""Read actual packed font variants through the X3 simulator."""
import json, os, shutil, subprocess, sys, tempfile, unittest
from pathlib import Path
import test_rework_font_boundaries as boundaries
from cai_dat_truoc_tenor import truoc_tenor
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
        self.settings=truoc_tenor(dict(language='VI',fontSize=26,sdFontFamilyName='Trial',readerInkWeightVersion=1,
                                       readerInkWeight=1,sleepTimeout=10))

    def test_installed_26_and_persistence(self):
        saved,log=self.run_sim('1000:CONFIRM;1700:CONFIRM;8500:QUIT')
        self.assertEqual(saved['fontSize'],26)
        self.assertEqual(saved['readerInkWeight'],1)
        self.assertIn('/weight-2/Trial_26.cpfont',log)
        self.assertIn('Entering activity: EpubReader',log)
        self.settings=saved
        saved,log=self.run_sim('1000:CONFIRM;1700:CONFIRM;8500:QUIT')
        self.assertEqual(saved['readerInkWeight'],1)

    def test_missing_variant_preserves_family(self):
        (self.sd/'.fonts/Trial/weight-2/Trial_26.cpfont').unlink()
        saved,log=self.run_sim('1000:CONFIRM;1700:CONFIRM;8500:QUIT')
        self.assertEqual((saved['sdFontFamilyName'],saved['fontSize'],saved['readerInkWeight']),('Trial',26,1))

    def test_corrupt_variant_preserves_family(self):
        (self.sd/'.fonts/Trial/weight-2/Trial_26.cpfont').write_bytes(b'broken')
        saved,log=self.run_sim('1000:CONFIRM;1700:CONFIRM;8500:QUIT')
        self.assertEqual((saved['sdFontFamilyName'],saved['readerInkWeight']),('Trial',1))

    def test_weight_change_returns_to_book(self):
        self.settings.update(fontSize=16,readerInkWeight=0)
        # The ink row now opens a value sheet before applying the selection.
        script = (self.READING + ';5000:DOWN;6000:RIGHT;6700:RIGHT;7400:RIGHT;8100:RIGHT;'
                  '9000:CONFIRM;9700:DOWN;10400:CONFIRM;12000:BACK;14000:QUIT')
        saved,log=self.run_sim(script)
        self.assertEqual(saved['readerInkWeight'],1)
        self.assertIn('/weight-2/Trial_16.cpfont',log)
        self.assertIn('Entering activity: TextSettings',log)

    def test_26_from_popup(self):
        self.settings.update(fontSize=14,readerInkWeight=0)
        # Nhip 17/09/2026: co chu gio chon trong the CO CHU cua Cua Cai dat van ban
        # (Phong | Co chu | Bo cuc | Kieu). READING toi the BO CUC; DOWN ba nhip di vong
        # vong qua dai the toi dong CUOI = co lon nhat cua ho Trial (26); CONFIRM ap dung.
        # The CO CHU cua ho Trial co tam dong 12..26 (goi --trial-weights sinh ra) va con tro dat tai
        # co dang dung 14 (dong 2), nen RIGHT sau nhip moi toi duoc dong 8 = 26; di dai the (LEFT)
        # vong qua vong 0 chu khong toi dong cuoi.
        # Y dinh bai giu nguyen: chon 26 roi quay lai sach.
        saved,log=self.run_sim(self.READING+';5000:DOWN;5800:DOWN;6600:DOWN;'
                                          '7400:RIGHT;8000:RIGHT;8600:RIGHT;9200:RIGHT;9800:RIGHT;10400:RIGHT;'
                                          '11200:CONFIRM;13000:BACK;15000:QUIT')
        self.assertEqual(saved['fontSize'],26)

if __name__=='__main__': unittest.main()
