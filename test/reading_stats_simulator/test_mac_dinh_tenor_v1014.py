"""Bo cai dat tenor/cross (v1.0.14) tren may mo phong, tu the nho toi file ghi lai.

- The trong: file dau tien may ghi ra mang dung 14 gia tri cua bo, ngon ngu EN,
  lat trang Bluetooth tat, tu ngu 10 phut, gan nut goc; moi khoa khac bang file mac
  dinh cua ban truoc.
- File cua ban truoc (chua co tenorPresetVersion): 14 khoa sang bo, moi khoa khac giu,
  va chi mot lan: nguoi dung doi lai mot khoa thi lan khoi dong sau khong ep nua.
- File cu hon (textSpacingVersion 1, chua co dau thut dong va dau net muc): cac luot
  quy doi cu chay truoc, khong ghi de gia tri cua bo, ke ca o lan khoi dong thu hai.
"""

import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))

BO_TENOR = {
    'extraParagraphSpacing': 1, 'lineSpacing': 2, 'wordSpacing': 3, 'paragraphIndent': 2,
    'readerInkWeight': 1, 'frontButtonFollowOrientation': 1,
    'shortPwrBtn': 3, 'sleepScreen': 10, 'statusBarClock': 1, 'tiltMenuNavigation': 1,
    'tiltPageTurn': 1, 'tiltStrengthV': 0, 'tiltTabNavigation': 2,
}
DAU = ('textSpacingVersion', 'paragraphIndentVersion', 'readerInkWeightVersion', 'tenorPresetVersion')

# File ban truoc ghi ra tren the trong cua may mo phong X3 UC8279.
BAN_TRUOC = {
    'textSpacingVersion': 3, 'paragraphIndentVersion': 1, 'readerInkWeightVersion': 1, 'uiTheme': 4,
    'uiTextSize': 0, 'globalStatusBarMode': 0, 'tenorSideArrows': 1, 'tenorButtonSymbols': 1,
    'hideBatteryPercentage': 0, 'refreshFrequency': 3, 'fadingFix': 0, 'screenInverted': 0, 'sleepScreen': 8,
    'sleepScreenCoverMode': 0, 'sleepScreenCoverFilter': 0, 'quickResumeSleepScreen': 0, 'wakeIntoBook': 0,
    'sleepBwRefresh': 1, 'fontFamily': 0, 'lineSpacing': 0, 'letterSpacing': 0, 'wordSpacing': 0,
    'extraParagraphSpacing': 0, 'paragraphAlignment': 0, 'screenMargin': 5, 'paragraphIndent': 1,
    'embeddedStyle': 1, 'dropCapMode': 1, 'hyphenationEnabled': 0, 'orientation': 0, 'readerInkWeight': 0,
    'textAntiAliasing': 1, 'imageRendering': 0, 'readerStatusBarMode': 2, 'sideButtonLayout': 0,
    'tiltPageTurn': 0, 'tiltTabNavigation': 0, 'tiltMenuNavigation': 0, 'tiltStrengthH': 1, 'tiltStrengthV': 1,
    'frontButtonFollowOrientation': 0, 'keyboardAligned': 1, 'keyboardAxisSwapped': 1, 'deviceName': '',
    'longPressButtonBehavior': 1, 'longPressMenuFunction': 1, 'shortPwrBtn': 0, 'pwrBtnFootnoteBack': 1,
    'sleepTimeoutMinutes': 10, 'showHiddenFiles': 0, 'removeReadBooksFromRecents': 0,
    'moveFinishedToReadFolder': 0, 'opdsDownloadFolder': '', 'opdsFilenameFormat': 0, 'frontlightOn': 0,
    'statusBarChapterPageCount': 1, 'statusBarBookProgressPercentage': 1, 'statusBarProgressBar': 2,
    'statusBarProgressBarThickness': 1, 'statusBarTitle': 1, 'statusBarBattery': 1, 'xtcStatusBarMode': 0,
    'statusBarClock': 0, 'clockAutoTimezone': 1, 'clockUtcOffsetQ': 48, 'clockFormat': 0,
    'clockHasBeenSynced': 0, 'statusBarItemsMode': 2, 'frontButtonBack': 0, 'frontButtonConfirm': 1,
    'frontButtonLeft': 2, 'frontButtonRight': 3, 'fontSize': 16, 'sdFontFamilyName': '', 'language': 'EN',
    'blePageTurnerEnabled': 0, 'blePeerAddr': '', 'blePeerName': '', 'blePrevKeyUsage': 0, 'bleNextKeyUsage': 0,
}

# Mot chu may tu chinh cua minh truoc khi len ban moi.
CUA_CHU_MAY = dict(BAN_TRUOC, sleepScreen=3, lineSpacing=0, language='VI', blePageTurnerEnabled=1,
                   blePeerAddr='0A:1B:2C:3D:4E:5F', blePeerName='Page remote', sleepTimeoutMinutes=31,
                   frontButtonBack=1, frontButtonConfirm=0, frontButtonLeft=3, frontButtonRight=2,
                   sideButtonLayout=1, uiTextSize=1, refreshFrequency=1, screenMargin=15, fontSize=18,
                   tiltStrengthH=2, clockUtcOffsetQ=76)


class MacDinhTenorTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='mac-dinh-tenor-')
        self.addCleanup(self.tmp.cleanup)
        self.sd = Path(self.tmp.name)
        self.file = self.sd / '.crosspoint/settings.json'

    def khoi_dong(self, cai_dat=None):
        if cai_dat is not None:
            self.file.parent.mkdir(exist_ok=True)
            self.file.write_text(json.dumps(cai_dat))
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_INPUT_SCRIPT='1500:QUIT')
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=30)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        return json.loads(self.file.read_text()), run.stdout + run.stderr

    def dung_bo_tenor(self, luu, log):
        for khoa, gia_tri in BO_TENOR.items():
            self.assertEqual(luu.get(khoa), gia_tri, f'{khoa}\n{log[-2000:]}')
        self.assertEqual(luu.get('tenorPresetVersion'), 1)

    def giu_nguyen(self, truoc, luu):
        for khoa, gia_tri in truoc.items():
            # uiTheme: v1.0.52 no longer saves it. sleepBwRefresh: v1.0.52 reads and saves the switch as
            # sleepBwFold, so the old key (1 on every card, the old default) is dropped and the fold
            # starts off (checked below).
            if khoa in BO_TENOR or khoa in DAU or khoa in ('uiTheme', 'sleepBwRefresh'):
                continue
            self.assertEqual(luu.get(khoa), gia_tri, khoa)
        self.assertNotIn('sleepBwRefresh', luu)
        self.assertEqual(luu.get('sleepBwFold'), 0, 'an old file turned the black and white fold back on')

    def test_the_trong(self):
        luu, log = self.khoi_dong()
        self.dung_bo_tenor(luu, log)
        self.assertEqual((luu['language'], luu['blePageTurnerEnabled'], luu['sleepTimeoutMinutes']), ('EN', 0, 10))
        self.assertEqual([luu[k] for k in ('frontButtonBack', 'frontButtonConfirm', 'frontButtonLeft',
                                           'frontButtonRight', 'sideButtonLayout')], [0, 1, 2, 3, 0])
        self.giu_nguyen(BAN_TRUOC, luu)

    def test_file_ban_truoc_chi_dua_mot_lan(self):
        luu, log = self.khoi_dong(CUA_CHU_MAY)
        self.dung_bo_tenor(luu, log)
        self.giu_nguyen(CUA_CHU_MAY, luu)
        # Chu may doi lai hai khoa cua bo: lan khoi dong sau phai giu nguyen.
        luu.update(lineSpacing=0, sleepScreen=3, tiltPageTurn=0)
        lan_hai, _ = self.khoi_dong(luu)
        self.assertEqual((lan_hai['lineSpacing'], lan_hai['sleepScreen'], lan_hai['tiltPageTurn']), (0, 3, 0))
        self.assertEqual((lan_hai['wordSpacing'], lan_hai['shortPwrBtn']), (3, 3))

    def test_file_cu_hon_quy_doi_truoc(self):
        cu = {k: v for k, v in CUA_CHU_MAY.items() if k not in DAU}
        cu.update(textSpacingVersion=1, lineSpacing=1, extraParagraphSpacing=2, paragraphIndent=2, readerInkWeight=3)
        luu, log = self.khoi_dong(cu)
        self.dung_bo_tenor(luu, log)
        self.giu_nguyen({k: v for k, v in cu.items() if k != 'letterSpacing'}, luu)
        lan_hai, log = self.khoi_dong()
        self.dung_bo_tenor(lan_hai, log)


if __name__ == '__main__':
    unittest.main()
