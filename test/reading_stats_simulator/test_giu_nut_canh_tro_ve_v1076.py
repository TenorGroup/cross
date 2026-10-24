"""v1.0.76: "Giu nut lat trang khi doc" = "Nut canh: tro ve" (so luu 4).

Nut canh (DOWN/UP trong kich ban gia lap) lat trang khi NHA; giu du 700 ms thi lam viec cua Back: ra Home, khong lat
trang nao truoc do. Nut truoc (LEFT/RIGHT) giu van nhay chuong nhu "Nhay chuong". Tep luu 4 phai duoc doc lai la 4
(duong nap chung tung kep theo so nhan, ve mac dinh Nhay chuong).
"""
import unittest

import test_chapter_hold as nen  # module, not the class: its own cases must not be collected here

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


if __name__ == '__main__':
    unittest.main()
