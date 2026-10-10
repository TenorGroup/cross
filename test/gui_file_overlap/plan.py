"""Hanh trinh luong Gui file tren gia lap X3 (nut bam) va X4 Pro (cam ung): chooser, noi mang (danh sach, mat khau,
loi, dang noi), may chu da noi, diem phat, Calibre. Moi case la mot luot chay, nhieu anh chup."""
import json
import os
import subprocess
import tempfile
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parents[2]
SIZES = {'x3': (528, 792), 'x4pro': (480, 800)}
ENV = {'x3': 'simulator_x3_uc8279', 'x4pro': 'simulator_x4pro'}
LONG = 'W' * 31
NET_SHORT = 'Simulator WiFi (fake):-45:open;Nha Tuan 5G:-60:wpa2'
NET_LONG = f'{LONG}:-40:open;Simulator WiFi (fake):-45:open;Nha Tuan 5G:-60:wpa2'
X3_TO = '1500:UP;1800:RIGHT;2100:RIGHT;2400:RIGHT;2700:RIGHT;2900:RIGHT;3100:CONFIRM'
X4_TO = '3000:TAP:455,754;5000:TAP:240,117'
# Dong cua chooser tren X4 Pro theo co chu (y cham): ket noi, Calibre, diem phat, mang thu 2 trong danh sach
X4_ROW = {0: {'join': 100, 'calibre': 180, 'ap': 260, 'net2': 190},
          1: {'join': 110, 'calibre': 235, 'ap': 365, 'net2': 200},
          2: {'join': 125, 'calibre': 260, 'ap': 400, 'net2': 220}}


def program(device):
    return Path(os.environ.get(f'GUIFILE_{device.upper()}_PROGRAM', REPO / '.pio/build' / ENV[device] / 'program'))


def plan(device, tier, long):
    """Danh sach (state, script, shots{ten: ms}, env, saved_wifi)."""
    x3 = device == 'x3'
    T0 = 3600 if x3 else 5600
    to = X3_TO if x3 else X4_TO
    net = NET_LONG if long else NET_SHORT
    saved = LONG if long else 'Simulator WiFi (fake)'
    row = X4_ROW[tier]

    def pick(which):  # chon dong cua chooser; tra ve (su kien, tong do tre)
        if x3:
            moves = {'join': [], 'calibre': ['RIGHT'], 'ap': ['RIGHT', 'RIGHT']}[which]
            ev = [f'{T0 + 900 * i}:{m}' for i, m in enumerate(moves)]
            return ev + [f'{T0 + 900 * len(moves)}:CONFIRM'], 900 * len(moves)
        return [f'{T0}:TAP:240,{row[which]}'], 0

    out = [('chooser', to, {'chooser': T0}, {}, None)]
    ev, d = pick('join')
    out.append(('sta-list', to + ';' + ';'.join(ev), {'list': T0 + d + 2500}, {'CROSSPOINT_SIM_WIFI_NETWORKS': net}, None))
    if x3:
        sel = [f'{T0 + 2600}:CONFIRM'] if long else [f'{T0 + 2600}:DOWN', f'{T0 + 3500}:CONFIRM']
        sel_open = [f'{T0 + 2600}:CONFIRM']
    else:
        sel = [f'{T0 + 2600}:TAP:240,{row["net2"] if long else 266}']
        sel_open = [f'{T0 + 2600}:TAP:240,116']
    pwnet = f'{LONG}:-40:wpa2;Simulator WiFi (fake):-45:open' if long else NET_SHORT
    out.append(('sta-password', to + ';' + ';'.join(ev + sel), {'password': T0 + 6500},
                {'CROSSPOINT_SIM_WIFI_NETWORKS': pwnet}, None))
    for mode in ('fail', 'timeout'):
        out.append((f'sta-{mode}', to + ';' + ';'.join(ev + sel_open), {'a': T0 + 4800, 'b': T0 + 8500},
                    {'CROSSPOINT_SIM_WIFI_NETWORKS': net, 'CROSSPOINT_SIM_WIFI_CONNECT': mode}, None))
    srv, more = {'p1': T0 + 3500}, []
    if x3:
        more, srv['p2'] = [f'{T0 + 3700}:DOWN'], T0 + 5500
    out.append(('sta-server', to + ';' + ';'.join(ev + more), srv, {'CROSSPOINT_SIM_WIFI_NETWORKS': net}, saved))
    ev, d = pick('ap')
    ap, more = {'step1': T0 + d + 4000}, []
    if x3:
        more, ap['step2'] = [f'{T0 + d + 4200}:RIGHT'], T0 + d + 6200
    out.append(('ap', to + ';' + ';'.join(ev + more), ap, {}, None))
    ev, d = pick('calibre')
    out.append(('calibre', to + ';' + ';'.join(ev), {'calibre': T0 + d + 4500}, {}, 'Simulator WiFi (fake)'))
    return out


def run_case(device, tier, lang, tips, long, state, script, shots, env, saved, port, keep=None, extra=None):
    """Chay mot luot, tra ve ({ten: Image 1 bit}, log). keep: thu muc de giu log va bmp (khong thi dung thu muc tam)."""
    with tempfile.TemporaryDirectory(prefix='gf-') as tmp:
        sd = Path(tmp) / 'sd'
        out = Path(keep) if keep else Path(tmp) / 'out'
        out.mkdir(parents=True, exist_ok=True)
        store = sd / '.crosspoint'
        store.mkdir(parents=True)
        (sd / 'sach').mkdir()
        settings = {'language': lang, 'uiShell': 0, 'uiTheme': 4, 'uiTextSize': tier, 'globalStatusBarMode': 0,
                    'tipsEnabled': tips, 'sleepTimeout': 120, 'readerTapTip': 0, 'sdFontFamilyName': ''}
        if long:
            settings['deviceName'] = LONG
        settings.update(extra or {})
        (store / 'settings.json').write_text(json.dumps(settings))
        (store / 'state.json').write_text(json.dumps({'openEpubPath': '', 'lastSleepFromReader': False,
                                                      'showBootScreen': False, 'readerActivityLoadCount': 0}))
        (store / 'recent.json').write_text(json.dumps({'books': []}))
        if saved:
            (store / 'wifi.json').write_text(json.dumps({'lastConnectedSsid': saved, 'credentials': [{'ssid': saved}]}))
        names = list(shots)
        e = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        e.update(env)
        e.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_HTTP_PORT=str(port),
                 CROSSPOINT_SIM_INPUT_SCRIPT=(f'{script};' if script else '') + f'{max(shots.values()) + 600}:QUIT',
                 CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{shots[n]}:{out}/{shots[n]}.bmp' for n in names))
        r = subprocess.run([str(program(device))], cwd=REPO, env=e, capture_output=True, text=True, timeout=120)
        log = r.stdout + r.stderr
        (out / 'simulator.log').write_text(log)
        w, h = SIZES[device]
        imgs = {}
        for n in names:
            p = out / f'{shots[n]}.bmp'
            if not p.exists():
                continue
            im = Image.open(p).convert('L')
            if im.size == (h, w):
                im = im.rotate(90, expand=True)
            imgs[n] = im.point(lambda v: 255 if v >= 128 else 0)
        return imgs, log
