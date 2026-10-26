"""v1.1.0: "Giu nut lat trang khi doc" = "Nut canh: lui trang" (so luu 5, founder 26/10/2026).

Nut canh (DOWN/UP trong kich ban gia lap) lat trang NGAY khi bam. Giu du 700 ms thi trang ket thuc o trang TRUOC trang
dang hien luc bat dau bam: nhip bam da lat toi mot trang nen nhip giu lui hai, chi ve trang dich mot lan. Nhip bam da
lui (hay khong lat duoc) thi nhip giu lui cho dung dich, khong lui thua, khong lui qua trang dau sach. Nut truoc
(LEFT/RIGHT) giu van nhay chuong.
"""
import re
import unittest

import test_chapter_hold as nen  # module, not the class: its own cases must not be collected here

GIU_CANH_LUI = 5
NEXT_NEXT = 3


class SideHoldPrevTest(unittest.TestCase):
    setUp = nen.ChapterHoldTest.setUp
    tearDown = nen.ChapterHoldTest.tearDown
    chay = nen.ChapterHoldTest.chay
    moc = staticmethod(nen.ChapterHoldTest.moc)

    @staticmethod
    def ve_sau(log, tu_ms):
        """So lan ve trang sach sau moc tu_ms."""
        return [int(t) for t in re.findall(r'^\[(\d+)\] \[\w+\] \[ERS\] Rendered page', log, re.M) if int(t) >= tu_ms]

    def test_bam_ngan_lat_ngay(self):
        log = self.chay('1000:CONFIRM;3000:DOWN;5000:DOWN;7000:QUIT', longPressButtonBehavior=GIU_CANH_LUI)
        self.assertEqual([p for _, p in self.moc(log)], [0, 1, 2], f'moi nhip nut canh lat dung mot trang: {self.moc(log)}')

    def test_giu_ve_trang_truoc_trang_luc_bam_mot_lan_ve(self):
        # Doc toi trang 3; giu nut canh: nhip bam lat sang 4, nhip giu ket thuc o trang 2, ve mot lan.
        log = self.chay('1000:CONFIRM;3000:DOWN;4500:DOWN;6000:DOWN;7500:DOWN:1000;10500:QUIT',
                        longPressButtonBehavior=GIU_CANH_LUI)
        viTri = self.moc(log)
        self.assertEqual(viTri[-1], (0, 2), f'giu phai ve trang truoc trang luc bam (3): {viTri}')
        self.assertNotIn('Entering activity: Home', log.split('Entering activity: EpubReader', 1)[1])
        self.assertEqual(len(self.ve_sau(log, 8200)), 1, f'lui hai trang chi ve trang dich mot lan: {self.ve_sau(log, 7500)}')

    def test_giu_tu_dau_chuong_ve_cuoi_chuong_truoc(self):
        # Giu nut truoc sang dau chuong 2 (spine 1), roi giu nut canh: bam sang trang 1, giu ve trang cuoi chuong 1.
        log = self.chay('1000:CONFIRM;3000:RIGHT:1000;6000:DOWN:1000;9500:QUIT', longPressButtonBehavior=GIU_CANH_LUI)
        viTri = self.moc(log)
        self.assertIn((1, 0), viTri, f'setup: giu nut truoc phai sang chuong ke: {viTri}')
        spine, page = viTri[-1]
        self.assertEqual(spine, 0, f'giu nut canh o dau chuong phai ve chuong truoc: {viTri}')
        self.assertGreater(page, 0, f'va o trang cuoi chuong truoc: {viTri}')

    def test_giu_o_trang_dau_sach_khong_lui_qua(self):
        log = self.chay('1000:CONFIRM;3000:DOWN:1000;6000:QUIT', longPressButtonBehavior=GIU_CANH_LUI)
        self.assertEqual(self.moc(log)[-1], (0, 0), f'khong lui qua trang dau sach: {self.moc(log)}')

    def test_giu_nut_lui_khong_lui_thua(self):
        # Trang 2; giu nut lui (UP): nhip bam da lui ve 1, nhip giu khong lui them.
        log = self.chay('1000:CONFIRM;3000:DOWN;4500:DOWN;6000:UP:1000;9000:QUIT', longPressButtonBehavior=GIU_CANH_LUI)
        self.assertEqual(self.moc(log)[-1], (0, 1), f'nhip bam da lui thi nhip giu khong lui them: {self.moc(log)}')

    def test_sau_sau_giu_nut_nao_cung_lui(self):
        # Bo cuc "Sau / Sau": ca hai nut canh lat toi; giu UP cung ve trang truoc trang luc bam.
        log = self.chay('1000:CONFIRM;3000:UP;4500:UP;6000:UP:1000;9000:QUIT', longPressButtonBehavior=GIU_CANH_LUI,
                        sideButtonLayout=NEXT_NEXT)
        self.assertEqual(self.moc(log)[-1], (0, 1), f'Sau / Sau: giu ve trang truoc trang luc bam (2): {self.moc(log)}')

    def test_giu_nut_truoc_van_nhay_chuong(self):
        log = self.chay('1000:CONFIRM;3000:RIGHT:1000;6000:QUIT', longPressButtonBehavior=GIU_CANH_LUI)
        self.assertEqual(self.moc(log)[-1], (1, 0), f'giu nut truoc van sang dau chuong ke tiep: {self.moc(log)}')


if __name__ == '__main__':
    unittest.main()
