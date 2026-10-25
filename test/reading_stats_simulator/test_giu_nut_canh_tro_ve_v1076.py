"""v1.0.76: "Giu nut lat trang khi doc" = "Nut canh: tro ve" (so luu 4).

Nut canh (DOWN/UP trong kich ban gia lap) lat trang khi NHA; giu du 700 ms thi lam viec cua Back: ra Home, khong lat
trang nao truoc do. Nut truoc (LEFT/RIGHT) giu van nhay chuong nhu "Nhay chuong". Tep luu 4 phai duoc doc lai la 4
(duong nap chung tung kep theo so nhan, ve mac dinh Nhay chuong).
"""
import json
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

import test_chapter_hold as nen  # module, not the class: its own cases must not be collected here
import test_reader_format_journeys as journeys  # same: helpers only

GIU_CANH_TRO_VE = 4


class SideHoldBackTest(unittest.TestCase):
    # The chapter-hold journey's book and runner, without its own cases.
    setUp = nen.ChapterHoldTest.setUp
    tearDown = nen.ChapterHoldTest.tearDown
    chay = nen.ChapterHoldTest.chay
    moc = staticmethod(nen.ChapterHoldTest.moc)

    def test_bam_ngan_nut_canh_lat_mot_trang_khi_nha(self):
        log = self.chay('1000:CONFIRM;3000:DOWN;5000:DOWN;7000:QUIT', longPressButtonBehavior=GIU_CANH_TRO_VE)
        viTri = self.moc(log)
        self.assertEqual([p for _, p in viTri], [0, 1, 2], f'moi nhip nut canh lat dung mot trang: {viTri}')
        self.assertNotIn('Entering activity: Home', log.split('Entering activity: EpubReader', 1)[1])

    def test_giu_nut_canh_ve_home_khong_lat_trang(self):
        log = self.chay('1000:CONFIRM;3000:DOWN:1000;6000:QUIT', longPressButtonBehavior=GIU_CANH_TRO_VE)
        sau = log.split('Entering activity: EpubReader', 1)[1]
        self.assertIn('Entering activity: Home', sau, 'giu nut canh phai ra Home')
        viTri = self.moc(log)
        self.assertTrue(all(p == 0 and s == 0 for s, p in viTri), f'giu de tro ve khong duoc lat trang nao: {viTri}')

    def test_giu_nut_truoc_van_nhay_chuong(self):
        log = self.chay('1000:CONFIRM;3000:RIGHT:1000;6000:QUIT', longPressButtonBehavior=GIU_CANH_TRO_VE)
        viTri = self.moc(log)
        self.assertEqual(viTri[-1], (1, 0), f'giu nut truoc van sang dau chuong ke tiep: {viTri}')
        self.assertNotIn('Entering activity: Home', log.split('Entering activity: EpubReader', 1)[1])


class SideHoldBackPreviewTest(unittest.TestCase):
    """Giu nut canh trong XEM TRUOC (giu Doc thu o danh sach file) cung la viec cua Back: dong xem truoc, ve danh
    sach file. Vong xem truoc (ReaderActivity::handlePreviewInput) chay truoc vong thuong nen phai tu doc sideBack.
    TXT di duong EPUB (D17), XTC la doc rieng; ca hai dung chung ham nay."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='cross-preview-hold-')
        self.sd = Path(self.temporary.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        (self.sd / 'books').mkdir()
        (self.store / 'settings.json').write_text(json.dumps({
            'language': 'EN', 'fontSize': 14, 'sleepTimeout': 10, 'longPressButtonBehavior': GIU_CANH_TRO_VE}))

    def tearDown(self):
        self.temporary.cleanup()

    def xem_truoc(self, duoi):
        self.duoi = duoi
        sach = self.sd / 'books' / ('journey.' + duoi)
        if duoi == 'xtc':
            journeys.write_xtc(sach)
        else:
            sach.write_text(''.join(f'Paragraph {n:04d}. ' + 'A place in the book. ' * 8 + '\n\n' for n in range(100)))
        (self.store / 'recent.json').write_text(json.dumps({
            'books': [{'path': '/books/' + sach.name, 'title': 'Reader journey'}]}))
        # Mo xem truoc bang giu nut canh o danh sach file (900 ms), doi nha, roi giu nut canh lan nua (1000 ms).
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT='1600:DOWN;2400:CONFIRM;4000:DOWN:900;6500:DOWN:1000;9500:QUIT')
        run = subprocess.run([str(journeys.PROGRAM)], cwd=journeys.REPO, env=env, capture_output=True, text=True,
                             timeout=40)
        self.log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, self.log)
        self.assertIn('Preview: /books/' + sach.name, self.log)
        return self.log

    def da_thoat(self):
        """Thoi diem (ms) hoat dong doc dong lai SAU khi xem truoc mo, hoac None."""
        mo = re.search(r'^\[(\d+)\].*Preview: ', self.log, re.M)
        thoat = [int(t) for t in re.findall(r'^\[(\d+)\] \[DBG\] \[ACT\] Exiting activity: (?:Epub|Xtc)Reader', self.log, re.M)
                 if int(t) > int(mo.group(1))]
        return thoat[0] if thoat else None

    def kiem_tra(self, duoi):
        self.xem_truoc(duoi)
        thoat = self.da_thoat()
        self.assertIsNotNone(thoat, 'giu nut canh trong xem truoc phai dong xem truoc')
        self.assertGreaterEqual(thoat, 6500, 'xem truoc chi dong khi nut canh bi giu lan hai')
        self.assertLess(thoat, 9500, 'xem truoc phai dong truoc khi kich ban het (khong phai do QUIT)')
        self.assertNotIn('Entering activity: Home', self.log.split('Preview: ', 1)[1],
                         'dong xem truoc ve danh sach file, khong ve Home')

    def test_giu_nut_canh_dong_xem_truoc_xtc(self):
        self.kiem_tra('xtc')

    def test_giu_nut_canh_dong_xem_truoc_txt(self):
        self.kiem_tra('txt')


if __name__ == '__main__':
    unittest.main()
