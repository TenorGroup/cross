"""Danh sach mang Wi-Fi (X3 va X4 Pro): cuong do song la bieu tuong 4 nac, mang co mat khau co o khoa, khong con chu thich.

Do tren anh 1 bit cua gia lap, khong theo rect trong ma:
  - moi dong co song 22x18 (va khoa 12x16 neu co mat khau), dung tung diem anh voi hinh nuong trong scripts/icons/song.py
    cua dung nac theo rssi (-45 nac 4, -55 nac 3, -63 nac 2, -72 va -84 nac 1);
  - tam song va tam khoa lech tam dong <= 0,5 px; mep phai cac song bang nhau va thang cot gia tri (chu "khong thay") <= 1 px;
  - khong con dong chu thich duoi danh sach, ma nguon khong con chuoi "||||", "*", "+" ngoai nhanh ugly.

Chay: pio run -e simulator_x3_uc8279 -e simulator_x4pro, roi python3 test/wifi_signal_icon/test_wifi_signal_icon.py
WIFISIG_X3_PROGRAM / WIFISIG_X4PRO_PROGRAM chon ban dung khac; WIFISIG_DEVICES=x3 hoac x4pro chay mot may."""
import json
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / 'scripts' / 'icons'))
import song  # noqa: E402

SIZES = {'x3': (528, 792), 'x4pro': (480, 800)}
ENV = {'x3': 'simulator_x3_uc8279', 'x4pro': 'simulator_x4pro'}
SCRIPT = {'x3': ('1500:UP;1800:RIGHT;2100:RIGHT;2400:RIGHT;2700:RIGHT;2900:RIGHT;3100:CONFIRM;3600:CONFIRM;3800:CONFIRM', 6100),
          'x4pro': ('3000:TAP:455,754;5000:TAP:240,117;5600:TAP:240,100;5800:CONFIRM', 8100)}
NETWORKS = 'Nha Tuan 5G:-45:wpa2;Simulator WiFi (fake):-55:open;Cafe Hem:-63:wpa2;VNPT-2F:-72:wpa2;TP-Link_8A21:-84:wpa2'
SAVED = ['Nha Tuan 5G', 'Van phong cu']
# Thu tu tren man: da luu truoc (theo rssi), da luu khong thay, roi theo rssi. (nac, co khoa); None = chu "khong thay"
ROWS = [(4, True), None, (3, False), (2, True), (1, True), (1, True)]
LEGEND_BAND = {'x3': (640, 684, 100, 430), 'x4pro': (640, 712, 80, 400)}  # y0, y1, x0, x1: noi cu cua dong chu thich
_port = iter(range(29700, 29800))


def program(device):
    return Path(os.environ.get(f'WIFISIG_{device.upper()}_PROGRAM', REPO / '.pio/build' / ENV[device] / 'program'))


def capture(device, tier=0, lang='VI'):
    script, shot = SCRIPT[device]
    with tempfile.TemporaryDirectory(prefix='ws-') as tmp:
        sd = Path(tmp) / 'sd'
        store = sd / '.crosspoint'
        store.mkdir(parents=True)
        (sd / 'sach').mkdir()
        (store / 'settings.json').write_text(json.dumps({
            'language': lang, 'uiShell': 0, 'uiTheme': 4, 'uiTextSize': tier, 'globalStatusBarMode': 0, 'tipsEnabled': 1,
            'sleepTimeout': 120, 'readerTapTip': 0, 'sdFontFamilyName': ''}))
        (store / 'state.json').write_text(json.dumps({'openEpubPath': '', 'lastSleepFromReader': False,
                                                      'showBootScreen': False, 'readerActivityLoadCount': 0}))
        (store / 'recent.json').write_text(json.dumps({'books': []}))
        (store / 'wifi.json').write_text(json.dumps({'lastConnectedSsid': '',
                                                     'credentials': [{'ssid': s, 'password': 'x'} for s in SAVED]}))
        bmp = Path(tmp) / 'list.bmp'
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_WIFI_NETWORKS=NETWORKS,
                   CROSSPOINT_SIM_HTTP_PORT=str(next(_port)),
                   CROSSPOINT_SIM_INPUT_SCRIPT=f'{script};{shot + 600}:QUIT',
                   CROSSPOINT_SIM_SCREENSHOTS=f'{shot}:{bmp}')
        subprocess.run([str(program(device))], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
        im = Image.open(bmp).convert('L')
        w, h = SIZES[device]
        if im.size == (h, w):
            im = im.rotate(90, expand=True)
        return np.array(im) < 128


def row_centres(ink, device):
    """Tam 6 dong dau, tu hinh hoc cua dong. X3 = vien con tro dong dau (cao h, buoc h + 4). X4 Pro = vach ke giua
    2 dong (cach day dong 3 px, buoc do duoc tu 2 vach dau, dong cao buoc - 6)."""
    if device == 'x3':
        ys = [y for y in range(40, 300) if ink[y, 100:380].sum() > 250]
        groups, start = [], ys[0]
        for a, b in zip(ys, ys[1:] + [10 ** 9]):
            if b != a + 1:
                groups.append((start, a))
                start = b
        top, bottom = groups[0][0], groups[1][1]
        pitch = bottom - top + 1 + 4
        return [(top + bottom) / 2 + pitch * k for k in range(6)]
    rules = [y for y in range(90, 640) if ink[y, 40:440].sum() >= 190 and ink[y - 1, 40:440].sum() < 190
             and ink[y + 1, 40:440].sum() < 190]
    pitch = rules[1] - rules[0]
    return [rules[0] - 3 - (pitch - 6) / 2 - 0.5 + pitch * k for k in range(6)]


def blocks(ink, x0, x1, y0, y1, gap=3):
    """Khoi muc theo cot trong >= gap: [(trai, tren, phai, duoi)] tuyet doi, phai/duoi la diem anh muc cuoi."""
    sub = ink[y0:y1 + 1, x0:x1 + 1]
    cols = np.where(sub.any(axis=0))[0]
    out, start, prev = [], None, None
    for c in list(cols) + [10 ** 9]:
        if start is None:
            start = c
        elif c - prev > gap:
            rows = np.where(sub[:, start:prev + 1].any(axis=1))[0]
            out.append((x0 + start, y0 + rows[0], x0 + prev, y0 + rows[-1]))
            start = c
        prev = c
    return out


def measure(ink, device):
    """Tra ve (loi, so do). Cot gia tri = mep phai chu 'khong thay' o dong 2."""
    centres = row_centres(ink, device)
    errors, report = [], []
    right = {'x3': 480, 'x4pro': 450}[device]
    text = blocks(ink, right - 140, right, int(centres[1]) - 16, int(centres[1]) + 16, gap=12)
    column = max(b[2] for b in text)
    wave_edges = []
    for k, row in enumerate(ROWS):
        if row is None:
            continue
        level, lock = row
        c = centres[k]
        found = blocks(ink, right - 80, right, int(c) - 14, int(c) + 14)
        want = 2 if lock else 1
        if len(found) != want:
            errors.append(f'dong {k + 1}: co {len(found)} khoi muc, can {want}: {found}')
            continue
        wave = found[-1]
        expect = np.array(song.wave(level).convert('L')) < 128
        got = ink[wave[1]:wave[3] + 1, wave[0]:wave[2] + 1]
        if got.shape != expect.shape or (got != expect).any():
            errors.append(f'dong {k + 1}: song khong dung hinh nac {level}: {wave}')
        wave_edges.append(wave[2])
        shifts = [(wave[1] + wave[3]) / 2 - c]
        if lock:
            lk = found[0]
            expect = np.array(song.lock().convert('L')) < 128
            got = ink[lk[1]:lk[3] + 1, lk[0]:lk[2] + 1]
            if got.shape != expect.shape or (got != expect).any():
                errors.append(f'dong {k + 1}: khoa sai hinh: {lk}')
            shifts.append((lk[1] + lk[3]) / 2 - c)
        for s in shifts:
            if abs(s) > 0.5:
                errors.append(f'dong {k + 1}: tam lech {s:+.1f} px')
        report.append((k + 1, level, lock, wave[2], max(abs(s) for s in shifts)))
    if len(set(wave_edges)) > 1:
        errors.append(f'mep phai song khong bang nhau: {wave_edges}')
    elif wave_edges and abs(wave_edges[0] - column) > 1:
        errors.append(f'mep phai song {wave_edges[0]} lech cot gia tri {column} qua 1 px')
    y0, y1, x0, x1 = LEGEND_BAND[device]
    if ink[y0:y1, x0:x1].any():
        errors.append(f'con muc o noi dong chu thich: y {y0}-{y1}, x {x0}-{x1}')
    return errors, report, column


class WifiSignalIcon(unittest.TestCase):
    def test_list_pixels(self):
        devices = [d for d in os.environ.get('WIFISIG_DEVICES', 'x3,x4pro').split(',') if program(d).exists()]
        self.assertTrue(devices, 'chua dung gia lap: pio run -e simulator_x3_uc8279 -e simulator_x4pro')
        bad = []
        for device in devices:
            for tier in (0, 1, 2):
                errors, report, column = measure(capture(device, tier), device)
                print(device, 'co chu', tier, 'cot gia tri', column, report)
                bad += [f'{device} co chu {tier}: {e}' for e in errors]
        self.assertEqual(bad, [])

    def test_old_marks_gone_from_source(self):
        text = (REPO / 'src/activities/network/WifiSelectionActivity.cpp').read_text()
        self.assertEqual(text.count('STR_NETWORK_LEGEND'), 1)
        self.assertRegex(text, r'if \(shell::isUgly\(\)\) \{\s*GUI.drawHelpText\([^;]*STR_NETWORK_LEGEND')
        self.assertNotIn('getSignalStrengthIndicator', text)
        ugly = re.search(r'std::string uglyStatus.*?\n}\n', text, re.S)
        outside = text.replace(ugly.group(0), '') if ugly else text
        for mark in ('"||||"', '" |||"', '"* "', '"+ "'):
            self.assertNotIn(mark, outside, mark)


if __name__ == '__main__':
    unittest.main()
