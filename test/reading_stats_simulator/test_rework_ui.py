"""Bai kiem nhip bam cua dot rework UI 14/09/2026.

Moi bai chay simulator X3 that voi mot the gia rieng. Kich ban bam nut la chuoi
`<ms>:<NUT>` cua CROSSPOINT_SIM_INPUT_SCRIPT, giu nut thi them `:<ms giu>`.
Con so nhip trong ten bai la HOP DONG: doi so nhip la doi bai kiem.

Do lai 16/09/2026 bang log activity cua simulator (khong suy dien):

* Home -> MOT nhip Chon o 1000 la mo luon cuon gan nhat (`Entering activity:
  EpubReader` o ~1085 ms). Nhip Chon THU HAI (~2800) moi mo menu doc
  (`Entering activity: EpubReaderMenu` o ~2890 ms). Menu phai duoc mo bang mot
  nhip rieng, khong duoc nhet vao luc mo sach.
* Menu doc co bon the: Yeu thich, Vi tri, Doc, Cong cu; doi the bang UP/DOWN.
  The Doc nam sau DOWN x2 va con tro dung SAN o dong 1 - khong co nhip "buoc
  xuong dong dau" nao nua. Bon hang cua the Doc, dung thu tu: 1 Cai dat van
  ban, 2 Che do ban dem, 3 Xoay man hinh, 4 Tu lat trang.
* Hai hang "Co chu" va "Font chu" da bi bo khoi menu doc theo ke hoach T3
  ("Text appearance lives in Text Settings. Keep the reader menu concise.").
  Doi co chu/font di qua man Cai dat van ban.
* Man Cai dat van ban duoc goi tu menu doc mo o the Bo cuc
  (`EpubReaderActivity.cpp`: TEXT_SETTINGS -> `TextSettingsActivity::Tab::Layout`).
  Tu the Bo cuc: UP mot nhip la the Co chu (con tro dung san o co 14 pt dang
  dung), UP hai nhip la the Ho font (con tro o ho dang dung).
* Giu nut lat trang doi co chu da BI GO (xem `CrossPointSettings.cpp`: gia tri
  FONT_SIZE_STEP cu bi chuyen ve OFF khi nap settings).
"""
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
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

    # Mo sach o 1000 (Home -> Chon = cuon gan nhat), mo menu doc o 2800, roi DOWN x2 sang
    # the Doc voi con tro dung o dong 1. Moi nhip cach nhau ~600 ms.
    OPEN_BOOK = '1000:CONFIRM'
    MO_MENU = ';2800:CONFIRM'
    THE_DOC = OPEN_BOOK + MO_MENU + ';3400:DOWN;4000:DOWN'

    def test_j2_che_do_ban_dem_8_nhip_va_quay_lai_dong_menu_mot_nhip(self):
        # The Doc, dong 1 Cai dat van ban, dong 2 Che do ban dem: tu dong 1 RIGHT mot nhip
        # sang dong 2, Chon doi tai cho (khong mo man nao), roi Quay lai mot nhip dong menu
        # va ve thang trang sach.
        script = (self.THE_DOC + ';4600:RIGHT;5200:CONFIRM;5800:BACK;8000:QUIT')
        log = self.run_sim(script)
        self.assertEqual(log.count('Entering activity: EpubReaderMenu'), 1, log)
        self.assertEqual(log.count('Exiting activity: EpubReaderMenu'), 1, log)
        self.assertEqual(self.saved()['screenInverted'], 1, log)

    def test_h4_man_cai_dat_van_ban_mo_the_co_chu_tai_co_dang_dung(self):
        # The Doc, dong 1 Cai dat van ban: man van ban mo o the Bo cuc (xem
        # EpubReaderActivity.cpp, TEXT_SETTINGS -> Tab::Layout). UP mot nhip sang the
        # Co chu, va con tro dang dung san o 14 pt: RIGHT sang 16 pt, Chon ap dung,
        # Quay lai mot nhip ve thang sach, khong mo lai menu.
        script = (self.THE_DOC + ';4600:CONFIRM;5600:UP;6400:RIGHT;7000:CONFIRM;8600:BACK;10800:QUIT')
        log = self.run_sim(script)
        self.assertEqual(log.count('Entering activity: TextSettings'), 1, log)
        self.assertEqual(log.count('Exiting activity: TextSettings'), 1, log)
        self.assertEqual(self.saved()['fontSize'], 16, log)
        # Ve thang sach, khong mo lai menu.
        self.assertEqual(log.count('Entering activity: EpubReaderMenu'), 1, log)

    # --- S1: man Cai dat khong thanh the, nut canh nhay nhom, vong chi gom dong ---------------
    # Vao Cai dat > nhom Khac tu man chinh: the Cai dat (DOWN x4), roi RIGHT x7
    # sang hang "Khac" trong danh sach nhom, Chon mo thang nhom do.
    VAO_THIET_BI = ('1000:DOWN;1500:DOWN;2000:DOWN;2500:DOWN;'
                    '3000:RIGHT;3250:RIGHT;3500:RIGHT;3750:RIGHT;4000:RIGHT;4250:RIGHT;4500:RIGHT;'
                    '4900:CONFIRM')

    def test_s1_nut_canh_sang_nhom_khac_dap_dong_1_va_chon_mo_ngay(self):
        # Nhóm Khác, dòng 1 là Đồng bộ KOReader; Chọn mở ngay, không phải bước vào dòng trước.
        log = self.run_sim(self.VAO_THIET_BI + ';6000:CONFIRM;8000:QUIT')
        self.assertEqual(log.count('Entering activity: Settings'), 1, log)
        self.assertEqual(log.count('Entering activity: KOReaderSettings'), 1, log)

    def test_s1_nhay_di_nhay_ve_con_cho_cu(self):
        # Display row 3 is Side arrows. Visiting Reader and returning keeps it focused.
        script = ('1000:UP;1600:RIGHT;2200:CONFIRM;'
                  '3000:RIGHT;3500:RIGHT;4000:CONFIRM;'
                  '4700:DOWN;5300:UP;5900:CONFIRM;7000:QUIT')
        self.settings['tenorSideArrows'] = 1
        (self.store / 'settings.json').write_text(json.dumps(self.settings))
        log = self.run_sim(script)
        self.assertEqual(self.saved()['tenorSideArrows'], 1, log)

    def test_s1_activating_another_group_resets_previous_cursor(self):
        # After activating Reader/Text settings, Display returns to row 1.
        # LEFT wraps to its last row: Quick resume timeout.
        script = ('1000:UP;1600:RIGHT;2200:CONFIRM;'
                  '3000:RIGHT;3500:RIGHT;4200:DOWN;4800:CONFIRM;6200:BACK;'
                  '7600:UP;8200:LEFT;8800:CONFIRM;10000:QUIT')
        self.settings['quickResumeSleepScreen'] = 0
        (self.store / 'settings.json').write_text(json.dumps(self.settings))
        log = self.run_sim(script)
        self.assertEqual(self.saved()['quickResumeSleepScreen'], 1, log)
        self.assertEqual(self.saved()['tenorSideArrows'], 1, log)

    def test_h6_folder_opens_child_then_back_reaches_root(self):
        # Folder rows open their directory; one Back returns to root inside FileBrowser.
        # A second Back exits FileBrowser. This covers access formerly reached by header hold.
        log = self.run_sim('1000:DOWN;1800:CONFIRM;2600:BACK;3400:BACK;4800:QUIT')
        self.assertEqual(log.count('Entering activity: FileBrowser'), 1, log)
        self.assertEqual(log.count('Exiting activity: FileBrowser'), 1, log)
        self.assertNotIn('Entering activity: EpubReader', log)

    def test_h6_folder_only_contains_files_and_folders(self):
        # With one books/ folder, RIGHT wraps to that same row; Confirm opens it.
        log = self.run_sim('1000:DOWN;1800:RIGHT;2400:CONFIRM;5500:QUIT')
        self.assertEqual(log.count('Entering activity: FileBrowser'), 1, log)
        self.assertNotIn('Entering activity: CrossPointWebServer', log)

    def test_file_transfer_from_home_settings(self):
        # Home Settings focuses Gửi file immediately. No intermediate Settings screen.
        log = self.run_sim('1000:UP;1800:CONFIRM;3000:BACK;4500:QUIT')
        self.assertIn('Entering activity: NetworkModeSelection', log)
        self.assertNotIn('Entering activity: Settings', log)
        self.assertEqual(log.count('Entering activity: Home'), 2, log)

    def test_opds_browser_from_other_settings(self):
        # Home Settings: previous row wraps to Other; its third row is OPDS browser.
        log = self.run_sim('1000:UP;1600:LEFT;2600:CONFIRM;'
                           '3200:RIGHT;3800:RIGHT;4400:CONFIRM;6000:QUIT')
        self.assertIn('Entering activity: OpdsServerList', log)

    # --- H5: thanh trang thai theo dung nguong 4, dong ho tach ra man rieng trong He thong ------
    def test_h5_clock_corners_change_directly_in_display(self):
        # Display row 5 cycles corner positions in place.
        script = ('1000:UP;1600:RIGHT;2200:CONFIRM;'
                  '3000:RIGHT;3500:RIGHT;4000:RIGHT;4500:RIGHT;5100:CONFIRM;6500:QUIT')
        self.settings['statusBarClock'] = 2
        (self.store / 'settings.json').write_text(json.dumps(self.settings))
        log = self.run_sim(script)
        self.assertNotIn('Entering activity: StatusBarSettings', log)
        self.assertEqual(self.saved()['statusBarClock'], 1, log)

    def test_h5_clock_follows_sleep_and_wake_in_system(self):
        # System: sleep row 1, wake row 2, clock row 3.
        script = ('1000:DOWN;1500:DOWN;2000:DOWN;2500:DOWN;'
                  '3000:RIGHT;3300:RIGHT;3600:RIGHT;3900:RIGHT;4300:CONFIRM;'
                  '5200:RIGHT;5600:RIGHT;6100:CONFIRM;8000:QUIT')
        log = self.run_sim(script)
        self.assertEqual(log.count('Entering activity: DongHoSettings'), 1, log)

    # --- H2: chinh nhanh co chu va font ngay trong menu doc, va giu nut lat trang doi co ------
    def test_h2_j1_co_chu_7_nhip_bang_popup_tai_co(self):
        # DOI DUONG 16/09/2026: hang "Co chu"/"Font chu" da bi bo khoi menu doc (ke hoach T3),
        # nen doi co chu di qua man Cai dat van ban: the Doc > dong 1 Cai dat van ban (mo o the
        # Bo cuc) > UP sang the Co chu (con tro dung o 14 pt) > RIGHT 16 pt > Chon > Quay lai
        # mot nhip ve thang sach.
        script = (self.THE_DOC + ';4600:CONFIRM;5600:UP;6400:RIGHT;7000:CONFIRM;8600:BACK;10800:QUIT')
        log = self.run_sim(script)
        self.assertEqual(self.saved()['fontSize'], 16, log)
        self.assertEqual(log.count('Entering activity: TextSettings'), 1, log)
        self.assertEqual(log.count('Exiting activity: EpubReaderMenu'), 1, log)

    def test_h2_font_hai_ho_thi_chon_doi_tai_co_va_ve_sach(self):
        # DOI DUONG 16/09/2026: menu doc khong con hang "Font chu", nen doi ho font trong man
        # Cai dat van ban: the Doc > dong 1 Cai dat van ban (the Bo cuc) > UP x2 sang the Ho
        # font (con tro dung o ho dang dung) > RIGHT sang ho ke > Chon > Quay lai ve thang sach.
        script = (self.THE_DOC + ';4600:CONFIRM;5600:UP;6200:UP;6800:RIGHT;7400:CONFIRM;'
                  '8600:BACK;10800:QUIT')
        log = self.run_sim(script)
        self.assertEqual(self.saved()['fontFamily'], 1, log)
        self.assertEqual(log.count('Entering activity: TextSettings'), 1, log)
        self.assertEqual(log.count('Exiting activity: EpubReaderMenu'), 1, log)

    def test_h2_giu_nut_lat_trang_doi_co_chu_kep_o_bien(self):
        # TIEN DE DA BI GO 16/09/2026: shortcut "giu nut lat trang doi co chu" khong con duoc
        # chon nua. CrossPointSettings.cpp chuyen moi gia tri FONT_SIZE_STEP cu ve OFF ngay khi
        # nap settings ("Retire the experimental hold-to-resize shortcut without delaying page
        # turns"), nen mot the nho gia tri 3 (Co chu) phai khong con tac dung gi: giu Phai/Trai
        # khong doi co chu, khong lat trang du, va khong mo man nao.
        self.settings['longPressButtonBehavior'] = 3
        (self.store / 'settings.json').write_text(json.dumps(self.settings))
        script = (self.OPEN_BOOK + ';3000:RIGHT:900;5200:RIGHT:900;7400:RIGHT:900;9600:LEFT:900;'
                  '12000:QUIT')
        log = self.run_sim(script)
        self.assertEqual(self.saved()['fontSize'], 14, log)
        self.assertIn('Entering activity: EpubReader', log)
        self.assertEqual(log.count('Entering activity: EpubReaderMenu'), 0, log)
        self.assertEqual(log.count('Entering activity: TextSettings'), 0, log)

    def test_h2_cai_dat_van_ban_tu_menu_mo_bo_cuc_va_quay_lai_ve_thang_sach(self):
        # the Doc, dong 1 Cai dat van ban -> man mo o the Bo cuc; Quay lai mot nhip ve sach,
        # khong mo lai menu.
        script = (self.THE_DOC + ';4600:CONFIRM;5800:BACK;8000:QUIT')
        log = self.run_sim(script)
        self.assertEqual(log.count('Entering activity: TextSettings'), 1, log)
        self.assertEqual(log.count('Entering activity: EpubReaderMenu'), 1, log)
        self.assertEqual(log.count('Exiting activity: TextSettings'), 1, log)

    def test_h2_giu_nut_doi_co_chu_tren_txt_giu_vi_tri(self):
        # TIEN DE CU DA BI GO BO THEO QUYET DINH SAN PHAM: nut "giu de doi co chu" da nghi huu.
        # CrossPointSettings::fromJson ghi ro "Retire the experimental hold-to-resize shortcut
        # without delaying page turns": gap longPressButtonBehavior == FONT_SIZE_STEP (3) trong
        # file luu thi ep ve OFF khi nap. Vi vay giu nut lat trang KHONG duoc doi co chu nua, o ca
        # EPUB lan TXT; doi co chu di duong da duyet (Cai dat van ban trong menu doc / man Cai dat).
        # Bai nay giu vai tro chan lui: neu ai bat lai loi tat cu ma khong sua hop dong, no phai do.
        (self.sd / 'books/ghi-chu.txt').write_text('Dong chu mau de kiem giu nut doi co.\n' * 400)
        (self.store / 'recent.json').write_text(json.dumps({'books': [{'path': '/books/ghi-chu.txt', 'title': 'Ghi chu'}]}))
        self.settings['longPressButtonBehavior'] = 3
        (self.store / 'settings.json').write_text(json.dumps(self.settings))
        log = self.run_sim('1000:CONFIRM;4000:RIGHT:1500;8000:QUIT')
        self.assertIn('Entering activity: TxtReader', log)
        # 1) Gia tri cu trong file bi ep ve OFF ngay khi nap (migration co chu y).
        self.assertEqual(self.saved()['longPressButtonBehavior'], 0, log)
        # 2) Giu nut khong doi co chu va khong lam mat trang dang doc.
        self.assertEqual(self.saved()['fontSize'], 14, log)
        self.assertEqual(log.count('Exiting activity: TxtReader'), 0, log)


if __name__ == '__main__':
    unittest.main()
