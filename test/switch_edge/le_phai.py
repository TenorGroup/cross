"""Do mep phai cua cac phan tu cuoi dong (cong tac, dau ^v, ">") tren anh gia lap X3 (528x792) va X4 Pro (480x800).

Anh gia lap la 1 bit nen moi do tren muc that: thanh phan lien thong sau nguong 128.
catalog.json: man hinh theo may va co chu, moi man la mot day buoc phim/cham chay lai tu dau.
"""
import hashlib
import json
import os
import shutil
import subprocess
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

REPO = Path(__file__).resolve().parents[2]
CATALOG = json.loads((Path(__file__).parent / 'catalog.json').read_text())
STEP_MS = 1300
SWIPE = 'SWIPE:240,650,240,150,200'
SW_SIZES = {(46, 26), (54, 30), (62, 34)}      # cong tac: 46 x 26 va hai co lon cua X3
PROGRAMS = {'x4': (os.environ.get('X4PRO_PROGRAM'), '.pio/build/simulator_x4pro/program'),
            'x3': (os.environ.get('X3_PROGRAM'), '.pio/build/simulator_x3_uc8279/program')}


def program(device):
    env, default = PROGRAMS[device]
    return Path(env) if env else REPO / default


def play(device, folder, steps, size):
    """Chay gia lap voi cac buoc ('TAP:x,y', 'SWIPE:..', 'KEY:NAME', 'WAIT', 'SHOT'); tra ve anh o moi 'SHOT'."""
    folder = Path(folder)
    shutil.rmtree(folder, ignore_errors=True)
    sd = folder / 'sd'
    (sd / '.crosspoint').mkdir(parents=True)
    (sd / 'sach').mkdir()
    books = []
    for name in ('test_kerning_ligature.epub', 'test_dictionary_synonyms.epub'):
        shutil.copyfile(REPO / 'test/epubs' / name, sd / 'sach' / name)
        books.append(dict(path='/sach/' + name, title=name[:-5]))
    (sd / '.crosspoint/state.json').write_text(json.dumps(dict(openEpubPath='', showBootScreen=False)))
    base = dict(language='VI', sdFontFamilyName='', readerTapTip=0, tipsEnabled=0, uiTextSize=size)
    if device == 'x3':
        base.update(tenorPresetVersion=1, uiShell=0, uiTheme=4, tenorButtonSymbols=1, tenorSideArrows=1,
                    uiShellSleepMemo=0, uiShellClockOnce=1, sleepTimeout=10)
    (sd / '.crosspoint/settings.json').write_text(json.dumps(base))
    (sd / '.crosspoint/recent.json').write_text(json.dumps({'books': books}))
    t, script, shots = 3000, [], []
    for step in steps:
        if step == 'WAIT':
            t += STEP_MS
        elif step == 'SHOT':
            shots.append(t - STEP_MS + 900)
        else:
            script.append(f'{t}:{step[4:] if step.startswith("KEY:") else step}')
            t += STEP_MS
    env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
    env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(script + [f'{max(shots) + 600}:QUIT']),
               CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{folder}/{ms}.bmp' for ms in shots))
    run = subprocess.run([str(program(device))], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
    (folder / 'simulator.log').write_text(run.stdout + run.stderr)
    assert run.returncode == 0, run.stderr[-1500:]
    return [Image.open(folder / f'{ms}.bmp').convert('L') for ms in shots]


def screen_steps(device, size, name):
    """X4 Pro: man dau + 3 lan cuon. X3: man dau + 2 lan chon xuong 6 dong."""
    if device == 'x4':
        return CATALOG['x4'][str(size)][name] + ['SHOT'] + [SWIPE, 'SHOT'] * 3
    return CATALOG['x3'][name] + ['SHOT'] + ['KEY:RIGHT'] * 6 + ['SHOT'] + ['KEY:RIGHT'] * 6 + ['SHOT']


def screens(device, size):
    return list(CATALOG['x4'][str(size)]) if device == 'x4' else list(CATALOG['x3'])


def components(img, thr=128):
    a = np.array(img) < thr
    labels, _ = ndimage.label(a, structure=np.ones((3, 3)))
    out = []
    for i, sl in enumerate(ndimage.find_objects(labels), 1):
        y0, y1, x0, x1 = sl[0].start, sl[0].stop - 1, sl[1].start, sl[1].stop - 1
        out.append(dict(x0=x0, x1=x1, y0=y0, y1=y1, w=x1 - x0 + 1, h=y1 - y0 + 1, n=int((labels[sl] == i).sum())))
    return out


def trailing_elements(img):
    """Cac phan tu cuoi dong: 'sw' cong tac, 'ud' dau ^v (hai chevron chong), 'chev' dau '>' (hop bao muc)."""
    W, H = img.size
    cs = [c for c in components(img)
          if not (c['w'] <= 2 and c['h'] <= 2) and not (c['h'] <= 2 and c['w'] > 60) and (40 if W == 480 else 8) < (c['y0'] + c['y1']) / 2 < H - 70]
    far = 420 if W == 480 else 468
    out, used = [], set()
    for i, c in enumerate(cs):
        if (c['w'], c['h']) in SW_SIZES and c['x1'] >= far - 40:
            out.append(dict(kind='sw', x0=c['x0'], x1=c['x1'], y0=c['y0'], y1=c['y1']))
            used.add(i)
            used |= {j for j, d in enumerate(cs) if j != i and c['x0'] <= d['x0'] and d['x1'] <= c['x1']
                     and c['y0'] <= d['y0'] and d['y1'] <= c['y1']}
    rest = [(i, c) for i, c in enumerate(cs) if i not in used and c['x1'] >= far and 11 <= c['w'] <= 17 and c['h'] <= 12]
    for i, c in rest:
        for j, d in rest:
            if j > i and i not in used and j not in used and abs(d['x0'] - c['x0']) <= 2 and 0 < abs(d['y0'] - c['y0']) <= 14:
                top, bot = (c, d) if c['y0'] < d['y0'] else (d, c)
                out.append(dict(kind='ud', x0=min(c['x0'], d['x0']), x1=max(c['x1'], d['x1']), y0=top['y0'], y1=bot['y1']))
                used |= {i, j}
    ink = np.array(img) < 128
    for i, c in enumerate(cs):
        sub = ink[c['y0']:c['y1'] + 1, c['x0']:c['x1'] + 1]
        checker = not ((sub[:, 1:] & sub[:, :-1]).any() or (sub[1:] & sub[:-1]).any())  # nua muc xen ke: khong hai diem ke canh
        if i not in used and checker and 6 <= c['w'] <= 11 and 14 <= c['h'] <= 30 and c['n'] < c['w'] * c['h'] * 0.55 \
                and c['x0'] >= (430 if W == 480 else 470) and c['x1'] <= W - (12 if W == 480 else 20):
            out.append(dict(kind='chev', x0=c['x0'], x1=c['x1'], y0=c['y0'], y1=c['y1']))
    for e in out:
        e['cy'] = (e['y0'] + e['y1']) / 2
    return sorted(out, key=lambda e: (e['cy'], e['x0']))


def rule_lines(img, x0=40, x1=440):
    """X4 Pro: hang y cua ke cham giua hai dong va vong khung (50% diem den deu moi doan 50 px)."""
    a = np.array(img) < 128
    return [y for y in range(a.shape[0])
            if all(0.3 <= a[y, x:x + 50].mean() <= 0.7 for x in range(x0, x1 - 49, 50))
            and (a[y, x0:x1 - 1] != a[y, x0 + 1:x1]).mean() >= 0.85]   # cham xen ke, khong phai nen chu hay vanh cong tac


def row_center(img, cy):
    ys = rule_lines(img)
    above, below = [y for y in ys if y < cy], [y for y in ys if y > cy]
    return (max(above) + min(below)) / 2 if above and below else None


def vertical_offset(img, sw, elements, device):
    """Tam doc cong tac so voi tam dau ^v gan nhat cung luoi hang (px; None neu hang khong deu hoac khong co ^v).
    X4 Pro: do tu tam giua hai ke cua moi phan tu, nen hang cao hon chuan van so duoc; X3 (khong ke): theo buoc hang."""
    uds = [e for e in elements if e['kind'] == 'ud']
    if not uds:
        return None
    near = min(uds, key=lambda u: abs(u['cy'] - sw['cy']))
    if device == 'x4':
        a, b = row_center(img, sw['cy']), row_center(img, near['cy'])
        if a is None or b is None or abs(sw['cy'] - a) > 3 or abs(near['cy'] - b) > 3:
            return None
        return (sw['cy'] - a) - (near['cy'] - b)
    ys = sorted(u['cy'] for u in uds)
    steps = [y2 - y1 for y1, y2 in zip(ys, ys[1:]) if 40 <= y2 - y1 <= 90]
    if not steps:
        return None
    pitch = min(steps)
    d = sw['cy'] - near['cy']
    r = d - round(d / pitch) * pitch
    return r if abs(r) < 5 else None


def value_only_edges(img, elements):
    """Chu gia tri cuoi dong khong co dau nao theo sau: mep phai chu cuoi cung cua moi hang khong co phan tu cuoi dong
    (hang co chu o nua phai, tu cot T-40 tro di). Tra ve danh sach (y giua, x1)."""
    W, H = img.size
    cs = [c for c in components(img) if not (c['w'] <= 2 and c['h'] <= 2) and not (c['h'] <= 2 and c['w'] > 60)
          and (40 if W == 480 else 8) < (c['y0'] + c['y1']) / 2 < H - 70 and c['x1'] < W - (20 if W == 480 else 12)]
    bands = []
    for c in sorted(cs, key=lambda c: c['y0']):
        if bands and c['y0'] <= bands[-1][1] + 2:
            bands[-1][1] = max(bands[-1][1], c['y1'])
            bands[-1][2] = max(bands[-1][2], c['x1'])
        else:
            bands.append([c['y0'], c['y1'], c['x1']])
    out = []
    for y0, y1, x1 in bands:
        if any(y0 - 4 <= e['cy'] <= y1 + 4 for e in elements):
            continue
        out.append(((y0 + y1) / 2, x1))
    return out


def has_ellipsis(img, y0, y1, x0, x1):
    """Ba cham 2 x 2 hoac 3 x 3 thang hang cach 3..6 px o cuoi nhan: nhan bi cat bang '...'."""
    dots = [c for c in components(img) if c['w'] <= 3 and c['h'] <= 3 and y0 <= c['y0'] <= y1 and x0 <= c['x0'] <= x1]
    for a in dots:
        run = [d for d in dots if d['y1'] == a['y1'] and 0 <= d['x0'] - a['x0'] <= 14]
        if len(run) >= 3:
            return True
    return False
