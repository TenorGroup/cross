"""Bai kiem nhip bam cua dot rework UI 14/09/2026.

Moi bai chay simulator X3 that voi mot the gia rieng. Kich ban bam nut la chuoi
`<ms>:<NUT>` cua CROSSPOINT_SIM_INPUT_SCRIPT, giu nut thi them `:<ms giu>`.
Con so nhip trong ten bai la HOP DONG: doi so nhip la doi bai kiem.
"""
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PROGRAM = REPO / '.pio/build/simulator_x3_uc8279/program'
EPUB = REPO / 'test/epubs/test_kerning_ligature.epub'


class ReworkUiTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='cross-rework-ui-')
        self.sd = Path(self.tmp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        (self.sd / 'books').mkdir()
        shutil.copy(EPUB, self.sd / 'books/sach.epub')
        (self.store / 'recent.json').write_text(json.dumps({'books': [{'path': '/books/sach.epub', 'title': 'Sach'}]}))
        self.settings = {'language': 'VI', 'fontSize': 14, 'screenInverted': 0}
        (self.store / 'settings.json').write_text(json.dumps(self.settings))

    def tearDown(self):
        self.tmp.cleanup()

    def run_sim(self, script, timeout=40):
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_INPUT_SCRIPT=script)
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=timeout)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log)
        return log

    def saved(self):
        return json.loads((self.store / 'settings.json').read_text())

    # Mo sach o 1000 (Chon o thanh the -> dong 1, Chon mo sach), menu mo o 8000.
    OPEN_BOOK = '1000:CONFIRM;1600:CONFIRM'

    def test_j2_che_do_ban_dem_8_nhip_va_quay_lai_dong_menu_mot_nhip(self):
        # menu 1, the Doc 2 (DOWN DOWN), toi dong 4/6 bang 3 nhip LEFT (0->6->5->4), bat 1, Quay lai 1.
        script = (self.OPEN_BOOK + ';8000:CONFIRM;9000:DOWN;9600:DOWN;10200:LEFT;10800:LEFT;11400:LEFT;'
                  '12000:CONFIRM;13000:BACK;15000:QUIT')
        log = self.run_sim(script)
        self.assertEqual(log.count('Entering activity: EpubReaderMenu'), 1, log)
        self.assertEqual(log.count('Exiting activity: EpubReaderMenu'), 1, log)
        self.assertEqual(self.saved()['screenInverted'], 1, log)

    def test_h4_man_cai_dat_van_ban_mo_the_co_chu_tai_co_dang_dung(self):
        # menu 1, the Doc 2, dong 3 Cai dat van ban (RIGHT RIGHT) 2, Chon: mo the Bo cuc 1, nut canh UP sang the
        # Co chu 1: con tro DUNG TAI 14 pt (khong o thanh the). RIGHT 16 pt 1, Chon ap dung 1, Quay lai ve sach 1.
        script = (self.OPEN_BOOK + ';8000:CONFIRM;9000:DOWN;9600:DOWN;10200:CONFIRM;10800:RIGHT;11400:RIGHT;'
                  '12000:CONFIRM;13500:UP;14300:RIGHT;14900:CONFIRM;16300:BACK;18300:QUIT')
        log = self.run_sim(script)
        self.assertEqual(log.count('Entering activity: TextSettings'), 1, log)
        self.assertEqual(log.count('Exiting activity: TextSettings'), 1, log)
        self.assertEqual(self.saved()['fontSize'], 16, log)
        # Ve thang sach, khong mo lai menu.
        self.assertEqual(log.count('Entering activity: EpubReaderMenu'), 1, log)

    # --- S1: man Cai dat khong thanh the, nut canh nhay nhom, vong chi gom dong ---------------
    # Vao Cai dat > Thiet bi tu man chinh: the Cai dat (3 DOWN), Chon (dong 1 Hien thi),
    # 4 RIGHT toi Thiet bi, Chon mo. Con tro DAP DONG 1 (Ten thiet bi).
    VAO_THIET_BI = ('1000:DOWN;1500:DOWN;2000:DOWN;2600:CONFIRM;3200:RIGHT;3700:RIGHT;4200:RIGHT;4700:RIGHT;'
                    '5300:CONFIRM')

    def test_s1_nut_canh_sang_nhom_khac_dap_dong_1_va_chon_mo_ngay(self):
        # DOWN (nut canh) sang Khac, dong 1 la Dong bo KOReader; Chon mo ngay, khong phai buoc vao dong truoc.
        log = self.run_sim(self.VAO_THIET_BI + ';6500:DOWN;7300:CONFIRM;9000:QUIT')
        self.assertEqual(log.count('Entering activity: Settings'), 1, log)
        self.assertEqual(log.count('Entering activity: KOReaderSettings'), 1, log)

    def test_s1_nhay_di_nhay_ve_con_cho_cu(self):
        # Hien thi dong 5 (An % pin), sang Trinh doc roi ve lai bang nut canh: van dong 5.
        # Chon o dong 5: An % pin co 3 lua chon nen doi tai cho, 0 -> 1.
        script = ('1000:DOWN;1500:DOWN;2000:DOWN;2600:CONFIRM;3200:CONFIRM;'
                  '4000:RIGHT;4500:RIGHT;5000:RIGHT;5500:RIGHT;'
                  '6200:DOWN;6900:UP;7600:CONFIRM;9000:QUIT')
        log = self.run_sim(script)
        self.assertEqual(self.saved().get('hideBatteryPercentage'), 1, log)

    def test_s1_bam_di_trong_nhom_moi_thi_quen_cho_cu(self):
        # Hien thi dong 5, sang Trinh doc, BAM DI mot dong (nhan nhom moi), ve Hien thi: dap dong 1.
        # LEFT tu dong 1 quay vong toi dong 9 (Che do ban dem), Chon bat: screenInverted 0 -> 1.
        # Neu cho cu con nho (dong 5), LEFT toi dong 4 (Bo qua anh man ngu, 2 lua chon) va ban dem van 0.
        script = ('1000:DOWN;1500:DOWN;2000:DOWN;2600:CONFIRM;3200:CONFIRM;'
                  '4000:RIGHT;4500:RIGHT;5000:RIGHT;5500:RIGHT;'
                  '6200:DOWN;6900:RIGHT;7600:UP;8300:LEFT;9000:CONFIRM;10500:QUIT')
        log = self.run_sim(script)
        self.assertEqual(self.saved()['screenInverted'], 1, log)
        self.assertEqual(self.saved().get('hideBatteryPercentage', 0), 0, log)

    # --- H6: giu Chon o thanh the Folder mo FileBrowser tai goc, dong "Mo folder" khong con ----
    def test_h6_giu_chon_o_thanh_folder_mo_file_browser(self):
        # DOWN sang the Folder (con tro o thanh the), giu Chon 1000 ms.
        log = self.run_sim('1000:DOWN;1500:LEFT;2200:CONFIRM:1000;4500:QUIT')
        self.assertEqual(log.count('Entering activity: FileBrowser'), 1, log)
        # Nha nut khong gay hanh dong du: khong mo sach, khong mo man khac.
        self.assertEqual(log.count('Entering activity: EpubReader'), 0, log)

    def test_h6_folder_only_contains_files_and_folders(self):
        # With one books/ folder, RIGHT wraps to the tab, then CONFIRM selects
        # books/ and the second CONFIRM opens it. Transfer must never be a row.
        log = self.run_sim('1000:DOWN;1800:RIGHT;2400:CONFIRM;3000:CONFIRM;5500:QUIT')
        self.assertEqual(log.count('Entering activity: FileBrowser'), 1, log)
        self.assertNotIn('Entering activity: CrossPointWebServer', log)

    def test_file_transfer_from_home_settings(self):
        # Home Settings focuses Gửi file immediately. No intermediate Settings screen.
        log = self.run_sim('1000:UP;1800:CONFIRM;3000:BACK;4500:QUIT')
        self.assertIn('Entering activity: NetworkModeSelection', log)
        self.assertNotIn('Entering activity: Settings', log)
        self.assertNotIn('Turning on WiFi', log)
        self.assertEqual(log.count('Entering activity: Home'), 2, log)

    def test_opds_browser_from_other_settings(self):
        # Home Settings: up via tab focus wraps to Other; its third row is OPDS browser.
        log = self.run_sim('1000:UP;1600:LEFT;2000:LEFT;2600:CONFIRM;'
                           '3200:RIGHT;3800:RIGHT;4400:CONFIRM;6000:QUIT')
        self.assertIn('Entering activity: OpdsServerList', log)

    # --- H5: thanh trang thai theo dung nguong 4, dong ho tach ra man rieng trong He thong ------
    def test_h5_thanh_tien_do_ba_lua_chon_doi_tai_cho(self):
        # Cai dat > Trinh doc (dong 2 o man chinh) > dong 5 Tuy chinh thanh trang thai > dong 3 Thanh tien do.
        # Ba lua chon nen mot nhip Chon doi ngay tai cho, khong mo popup: mac dinh An (2) -> Sach (0).
        script = ('1000:DOWN;1500:DOWN;2000:DOWN;2600:CONFIRM;3200:RIGHT;3800:CONFIRM;'
                  '4600:LEFT;5200:CONFIRM;6500:RIGHT;7100:RIGHT;7700:CONFIRM;9000:QUIT')
        log = self.run_sim(script)
        self.assertEqual(log.count('Entering activity: StatusBarSettings'), 1, log)
        self.assertEqual(self.saved().get('statusBarProgressBar'), 0, log)

    def test_h5_cua_dong_ho_o_cuoi_he_thong(self):
        # Cai dat > He thong (dong 4) > LEFT tu dong 1 quay vong toi dong cuoi = Dong ho > Chon mo man Dong ho.
        script = ('1000:DOWN;1500:DOWN;2000:DOWN;2600:CONFIRM;3200:RIGHT;3700:RIGHT;4200:RIGHT;4800:CONFIRM;'
                  '5600:LEFT;6200:CONFIRM;8000:QUIT')
        log = self.run_sim(script)
        self.assertEqual(log.count('Entering activity: DongHoSettings'), 1, log)

    # --- H2: chinh nhanh co chu va font ngay trong menu doc, va giu nut lat trang doi co ------
    def test_h2_j1_co_chu_7_nhip_bang_popup_tai_cho(self):
        # menu 1, the Doc 2, dong 1 Co chu (Chon) 1, popup mo tai 14 pt 1, RIGHT 16 pt 1, Chon 1: ap dung, ve sach.
        script = (self.OPEN_BOOK + ';8000:CONFIRM;9000:DOWN;9600:DOWN;10200:CONFIRM;10800:CONFIRM;'
                  '12000:RIGHT;12600:CONFIRM;14500:QUIT')
        log = self.run_sim(script)
        self.assertEqual(self.saved()['fontSize'], 16, log)
        self.assertEqual(log.count('Entering activity: TextSettings'), 0, log)
        self.assertEqual(log.count('Exiting activity: EpubReaderMenu'), 1, log)

    def test_h2_font_hai_ho_thi_chon_doi_tai_cho_va_ve_sach(self):
        # May chi co 2 ho built-in (N < 4): dong Font chu doi ngay sang ho ke, dong menu, khong mo man nao.
        script = (self.OPEN_BOOK + ';8000:CONFIRM;9000:DOWN;9600:DOWN;10200:CONFIRM;10800:RIGHT;'
                  '11400:CONFIRM;13500:QUIT')
        log = self.run_sim(script)
        self.assertEqual(self.saved()['fontFamily'], 1, log)
        self.assertEqual(log.count('Entering activity: TextSettings'), 0, log)
        self.assertEqual(log.count('Exiting activity: EpubReaderMenu'), 1, log)

    def test_h2_giu_nut_lat_trang_doi_co_chu_kep_o_bien(self):
        # Lua chon thu tu cua "Giu nut lat trang khi doc" = 3 (Co chu). Giu Phai: 14 -> 16 -> 18 -> 18 (kep),
        # giu Trai: 18 -> 16. Khong mo menu, khong mo man nao.
        self.settings['longPressButtonBehavior'] = 3
        (self.store / 'settings.json').write_text(json.dumps(self.settings))
        script = (self.OPEN_BOOK + ';8000:RIGHT:900;10500:RIGHT:900;13000:RIGHT:900;15500:LEFT:900;18500:QUIT')
        log = self.run_sim(script)
        self.assertEqual(self.saved()['fontSize'], 16, log)
        self.assertEqual(log.count('Entering activity: EpubReaderMenu'), 0, log)
        self.assertEqual(log.count('Entering activity: TextSettings'), 0, log)

    def test_h2_cai_dat_van_ban_tu_menu_mo_bo_cuc_va_quay_lai_ve_thang_sach(self):
        # the Doc, dong 3 Cai dat van ban -> man mo o the Bo cuc; Quay lai mot nhip ve sach, khong mo lai menu.
        script = (self.OPEN_BOOK + ';8000:CONFIRM;9000:DOWN;9600:DOWN;10200:CONFIRM;10800:RIGHT;11400:RIGHT;'
                  '12000:CONFIRM;13500:BACK;15500:QUIT')
        log = self.run_sim(script)
        self.assertEqual(log.count('Entering activity: TextSettings'), 1, log)
        self.assertEqual(log.count('Entering activity: EpubReaderMenu'), 1, log)
        self.assertEqual(log.count('Exiting activity: TextSettings'), 1, log)

    def test_h2_giu_nut_doi_co_chu_tren_txt_giu_vi_tri(self):
        # TXT: giu Phai doi co 14 -> 16, chi muc trang dung lai, khong lat trang du, van dang o TxtReader.
        (self.sd / 'books/ghi-chu.txt').write_text('Dong chu mau de kiem giu nut doi co.\n' * 400)
        (self.store / 'recent.json').write_text(json.dumps({'books': [{'path': '/books/ghi-chu.txt', 'title': 'Ghi chu'}]}))
        self.settings['longPressButtonBehavior'] = 3
        (self.store / 'settings.json').write_text(json.dumps(self.settings))
        log = self.run_sim('1000:CONFIRM;1600:CONFIRM;5000:RIGHT:900;8000:QUIT')
        self.assertIn('Entering activity: TxtReader', log)
        self.assertEqual(self.saved()['fontSize'], 16, log)
        self.assertEqual(log.count('Exiting activity: TxtReader'), 0, log)


if __name__ == '__main__':
    unittest.main()
