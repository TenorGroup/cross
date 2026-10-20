#!/usr/bin/env python3
"""hanh_trinh.py <may> <thu-muc-ra> [--chang a,b] [--moc ket-qua.json] [--spike chang]

Journey suite over the probe's cable channel (env m-x3, m-x4c, x4pro-probe). Each step sends its
commands, then judges:
  - screen: the `[HEAP] enter <Activity>` line it must reach;
  - its own event (pages moved and came back, a chapter rebuilt, gray layers finished, text in a box);
  - the heap floor: `floor_refusals` from HEAP_INFO before/after, `HEAP_FLOOR` lines (door refusals and
    paint marks under the floor), gray layers refused for the heap, and on X3 the lowest free heap of the
    step from HEAP_LOW (any allocation, however short; the largest block has no such monitor);
  - no restart inside the step (HEAP_INFO `up` keeps growing);
  - with --moc on a full run: free heap at the step's end no more than 1,024 B under the baseline's; the
    sleep-image box keeps at least half the baseline's black.
States: XANH, DO, MANG (Wi-Fi never joined), CHUP_HONG (screenshot failed), THIEU (no heap reading).
The run is green only when every chosen step ran and is XANH; any other state exits 1.
Needs pyserial and Pillow. --spike <step> sends HEAP_SPIKE inside that step: that step must turn DO.
"""
import json, os, re, sys, time
import serial
from PIL import Image

MAY = {
    'x3': {'port': '/dev/cu.usbmodem83301', 'w': 792, 'h': 528, 'floor': True},
    'x4c': {'port': '/dev/cu.usbmodem1101', 'w': 800, 'h': 480, 'floor': False},
    'x4pro': {'port': '/dev/cu.usbmodem1C2904243F2C2', 'w': 800, 'h': 480, 'floor': False},
}
SACH = {'x3': '/v1015/v1015-thu-30.epub', 'x4c': '/v1069-thu-30.epub', 'x4pro': '/v1053-thu-30.epub'}
# The fixed card font every run reads with, so .68 and .69 compare (FONT_TEST sets it in RAM; it is the
# font each unit already uses).
FONT = {'x3': 'Bokerlam 18 0', 'x4c': 'Literata 14 0', 'x4pro': 'Bookerly 14 0'}
# The sleep-image box's text rows, as fractions of the portrait frame (y from, y to): the button units
# centre the box, the X4 Pro's touch sheet sits on the bottom edge.
HOP = {'x3': (0.36, 0.44), 'x4c': (0.36, 0.44), 'x4pro': (0.67, 0.79)}
ANH = {'x3': '/v1069-hanh-trinh.bmp', 'x4c': '/v1068-3994.bmp', 'x4pro': '/v1068-3994.bmp'}
FREE_FLOOR, LARGEST_FLOOR = 10240, 8180
MOC_LECH = 1024
# Steps whose heap is still moving when read (the reader caches pages behind the last turn): the same
# .68 image ended mo-sach 1,764 B and lat-40 3,884 B apart over two runs, the settled steps <= 368 B.
# The floor checks still apply to them; the baseline comparison does not.
CHUA_LANG = {'mo-sach', 'lat-40'}


def chang(may):
    """(name, commands, wait s, screen to reach or None, event check or None)."""
    s = SACH[may]
    return [
        ('home', ['HOME'], 4, 'Home', None),
        ('mo-sach', ['OPEN_BOOK ' + s], 9, 'EpubReader', 'vi-tri'),
        ('lat-40', ['PRESS NEXT 80 40 350'], 24, None, 'tien'),
        ('lui-40', ['PRESS PREV 80 40 350'], 24, None, 've-cho-cu'),
        ('xam', ['PRESS NEXT 80 6 3000'], 21, None, 'xam'),
        ('ra-home', ['HOME'], 5, 'Home', None),
        ('dung-lai', ['CLEAR_BOOK_CACHE ' + s, 'OPEN_BOOK ' + s, 'wait:12', 'PRESS NEXT 80 15 1000'], 8, 'EpubReader', 'dung-lai'),
        ('ra-home-2', ['HOME'], 5, 'Home', None),
        ('cai-dat', ['SETTINGS_READER'], 4, 'Settings', None),
        ('anh-ngu', ['OPEN_BOOK ' + ANH[may], 'wait:6', 'PRESS CONFIRM 80 1 300'], 5, 'Confirmation', 'hop-co-chu'),
        ('huy-anh', ['PRESS BACK 150 1 300'], 5, None, None),
        ('home-cuoi', ['HOME'], 4, 'Home', None),
    ]


class Cong:
    """The cable, reopened when the port drops (a USB reset re-enumerates it)."""

    def __init__(self, port, out):
        self.port = port; self.s = None
        self.t0 = time.time(); self.pend = b''; self.lines = []
        self.log = open(os.path.join(out, 'serial.log'), 'a')
        self.mo()

    def mo(self):
        for _ in range(30):
            try:
                self.s = serial.Serial(); self.s.port = self.port; self.s.baudrate = 115200; self.s.timeout = 0.05
                self.s.dtr = True; self.s.rts = False; self.s.open(); return
            except (serial.SerialException, OSError):
                time.sleep(1)
        raise SystemExit(f'khong mo duoc {self.port}')

    def doc(self, n):
        try:
            return self.s.read(n)
        except (serial.SerialException, OSError):
            self.ghi_log('!!! cong rot, mo lai'); self.s.close(); time.sleep(2); self.mo(); return b''

    def ghi_log(self, text):
        self.log.write(f'{(time.time()-self.t0)*1000:9.0f} {text}\n')

    def pump(self, sec):
        end = time.time() + sec
        while time.time() < end:
            d = self.doc(8192)
            if not d: continue
            self.pend += d
            while b'\n' in self.pend:
                line, self.pend = self.pend.split(b'\n', 1)
                text = line.decode('utf-8', 'replace').rstrip()
                self.lines.append(text); self.ghi_log(text)
        self.log.flush()

    def gui(self, cmd, cho=1.2):
        self.ghi_log('>>> CMD:' + cmd)
        try:
            self.s.write(('CMD:' + cmd + '\n').encode()); self.s.flush()
        except (serial.SerialException, OSError):
            self.s.close(); time.sleep(2); self.mo(); self.s.write(('CMD:' + cmd + '\n').encode())
        self.pump(cho)

    def hoi(self, cmd, pattern, tries=3):
        """Sends cmd until a line matches pattern (the unit may be busy drawing); the match or None."""
        start = len(self.lines)
        for _ in range(tries):
            self.gui(cmd)
            for _ in range(8):
                for l in self.lines[start:]:
                    m = re.search(pattern, l)
                    if m: return m
                self.pump(0.5)
        return None

    def heap(self):
        # Whole lines only: the X4 Pro's CDC port drops bytes when the log is busy.
        m = self.hoi('HEAP_INFO', r'HEAP_INFO:(free=\d+,.*,up=\d+)$', tries=5)
        return {k: int(v) for k, v in (kv.split('=') for kv in m.group(1).split(','))} if m else None

    def chup(self, path, w, h):
        self.pump(0.3)
        self.s.write(b'CMD:SCREENSHOT\n'); self.s.flush()
        buf, t = self.pend, time.time(); self.pend = b''
        while time.time() - t < 40:
            buf += self.doc(65536)
            m = re.search(rb'SCREENSHOT_START:(\d+)\n', buf)
            if m and len(buf) >= m.end() + int(m.group(1)):
                n = int(m.group(1)); data = buf[m.end():m.end() + n]
                # Log lines printed around the image still count for the step.
                self.pend = buf[:m.start()] + buf[m.end() + n:]; self.pump(0.5)
                if n != w * h // 8: return None
                im = Image.frombytes('1', (w, h), data)
                im = im.rotate(270, expand=True)  # the panel is portrait, as x3run.py shows it
                im.save(path); return im
        self.pend = buf; self.pump(0.1)
        return None


def den(im, box):
    return sum(1 for p in im.crop(box).getdata() if p == 0)


def vi_tri(lines):
    """(spine, page) of the last page drawn readable in these lines."""
    for l in reversed(lines):
        m = re.search(r'PAINT_READABLE_BOUND .* spine=(\d+) page=(\d+)', l)
        if m: return int(m.group(1)), int(m.group(2))
    return None


def su_kien(kiem, moi, im, nho, moc=None, hop=HOP['x3']):
    """(ok, note) of a step's own event."""
    if kiem == 'vi-tri':
        p = vi_tri(moi); nho['dau'] = p
        return p is not None, f'vi tri={p}'
    if kiem == 'tien':
        p = vi_tri(moi); nho['sau-lat'] = p
        return p is not None and nho.get('dau') is not None and p > nho['dau'], f"{nho.get('dau')} -> {p}"
    if kiem == 've-cho-cu':
        p = vi_tri(moi)
        return p is not None and p == nho.get('dau'), f"{nho.get('sau-lat')} -> {p}, can {nho.get('dau')}"
    if kiem == 'xam':
        # Pages 3 s apart: every text page's gray layers must finish (PAINT_GRAY_DONE); any skip is red.
        done = sum('PAINT_GRAY_DONE' in l for l in moi)
        reasons = [l.split('GRAY_SKIPPED', 1)[1].strip()[:40] for l in moi if 'GRAY_SKIPPED' in l]
        return done >= 6 and not reasons, f'gray_done={done} skipped={reasons}'
    if kiem == 'dung-lai':
        built = any('CLEAR_BOOK_CACHE:' in l and 'removed=1' in l for l in moi)
        failed = [l for l in moi if re.search(r'Failed to stream|Failed to build|section.*fail', l, re.I)]
        p = vi_tri(moi)
        return built and not failed and p is not None, f'cache xoa={built} vi tri={p} loi={len(failed)}'
    if kiem == 'hop-co-chu':
        # The sleep-image confirmation box must carry text in its upper half (cc4e5581 left it blank).
        if im is None: return False, 'khong co anh'
        # Against the baseline the box keeps at least half its black (the outline alone is about a quarter);
        # without one only an empty region fails.
        w, h = im.size; n = den(im, (w // 6, int(h * hop[0]), w * 5 // 6, int(h * hop[1]))); nho['den'] = n
        if moc and moc.get('den'): return n * 2 >= moc['den'], f"den vung chu={n}, moc {moc['den']}"
        return n > 200, f'den vung chu={n} (chua so moc)'
    return True, ''


def chay(may, out, chon=None, moc=None, spike=None):
    os.makedirs(out, exist_ok=True)
    cfg = MAY[may]; c = Cong(cfg['port'], out); c.pump(2)
    # TXTIMEOUT: the CDC ports drop bytes at the release's 1 ms write timeout (cut HEAP_INFO lines).
    for cmd in ('TXTIMEOUT 200', 'HOME', 'SET textAntiAliasing=1', 'FONT_TEST ' + FONT[may]):
        c.gui(cmd, 3)
    if not any(l.startswith('FONT_TEST:family=') for l in c.lines):
        raise SystemExit('FONT_TEST khong chay: font co dinh khong co tren the?')
    moc = {r['chang']: r for r in json.load(open(moc))} if moc else {}
    rows, nho = [], {}
    for ten, cmds, cho, man, kiem in chang(may):
        if chon and ten not in chon: continue
        truoc = c.heap(); start = len(c.lines)
        if cfg['floor']: c.gui('HEAP_LOW START')
        for cmd in cmds:
            if cmd.startswith('wait:'): c.pump(float(cmd[5:]))
            else: c.gui(cmd)
        c.pump(cho)
        if spike == ten:
            # Take the largest block as the heap is now: what is left is under the floor, so the door
            # refuses it and the step must turn red.
            now = c.heap()
            c.gui(f"HEAP_SPIKE {max(now['largest'] - 64, 4096)} 5" if now else 'HEAP_SPIKE 4096 5', 2)
        low = None
        if cfg['floor']:
            m = c.hoi('HEAP_LOW STOP', r'HEAP_LOW:min_free=(\d+)', tries=1)
            low = int(m.group(1)) if m else None
        sau = c.heap()
        im = c.chup(os.path.join(out, ten + '.png'), cfg['w'], cfg['h'])
        moi = c.lines[start:]
        vao = [m.group(1) for l in moi for m in [re.search(r'\[HEAP\] enter (\w+)', l)] if m]
        nho.pop('den', None)
        ev_ok, ghi = su_kien(kiem, moi, im, nho, moc.get(ten), HOP[may])
        loi = []
        if man and man not in vao: loi.append(f'khong vao {man}')
        if not ev_ok: loi.append('su kien')
        floor_lines = [l for l in moi if '[HEAP_FLOOR]' in l]
        if floor_lines: loi.append(f'HEAP_FLOOR x{len(floor_lines)}')
        gray_heap = [l for l in moi if re.search(r'GRAY_SKIPPED (heap_floor|scratch_oom|bw_store_oom)', l)]
        if gray_heap: loi.append(f'xam bi tu choi vi heap x{len(gray_heap)}')
        if cfg['floor'] and (low is None or low < FREE_FLOOR): loi.append(f'HEAP_LOW min_free={low}')
        if truoc and sau and sau['up'] < truoc['up']: loi.append('may khoi dong lai trong chang')
        if truoc and sau:
            for k in ('floor_refusals', 'floor_failures'):
                if sau.get(k) != truoc.get(k): loi.append(f"{k} {truoc.get(k)}->{sau.get(k)}")
            if cfg['floor'] and (sau['free'] < FREE_FLOOR or sau['largest'] < LARGEST_FLOOR):
                loi.append('duoi san')
            m = moc.get(ten)
            # The heap at a step depends on every step before it: compare only full runs.
            if chon is None and ten not in CHUA_LANG and m and m.get('sau') and sau['free'] < m['sau']['free'] - MOC_LECH:
                loi.append(f"thap hon moc {m['sau']['free'] - sau['free']} B")
        wifi_fail = any(re.search(r'WIFI.*(join|connect).*(fail|timeout)', l, re.I) for l in moi)
        if not (truoc and sau): trang = 'THIEU'
        elif im is None and not loi: trang = 'CHUP_HONG'
        elif loi and wifi_fail and not floor_lines: trang = 'MANG'
        else: trang = 'DO' if loi else 'XANH'
        rows.append({'chang': ten, 'trang': trang, 'vao': vao, 'can': man, 'truoc': truoc, 'sau': sau, 'low': low, 'den': nho.get('den'),
                     'loi': loi, 'ghi': ghi})
        h = f"free {sau['free']} largest {sau['largest']} low {low} refusals {sau['floor_refusals']}" if sau else '-'
        print(f'{ten:10s} {trang:9s} {h} {"; ".join(loi)} {ghi}', flush=True)
    json.dump(rows, open(os.path.join(out, 'ket-qua.json'), 'w'), ensure_ascii=False, indent=1)
    with open(os.path.join(out, 'BANG.md'), 'w') as f:
        f.write(f'# Hành trình {may} {time.strftime("%Y-%m-%d %H:%M")}\n\n')
        f.write('| Chặng | Kết quả | Màn vào | Trống/khối sau | Trống thấp nhất | Lỗi | Ghi |\n|---|---|---|---|---|---|---|\n')
        for r in rows:
            h = f"{r['sau']['free']}/{r['sau']['largest']} | {r['low']}" if r['sau'] else '- | -'
            f.write(f"| {r['chang']} | {r['trang']} | {','.join(r['vao'])} | {h} | {'; '.join(r['loi'])} | {r['ghi']} |\n")
    return rows


def tu_kiem():
    lines = ['x PAINT_READABLE_BOUND rid=1 spine=0 page=3 kind=x', 'y PAINT_READABLE_BOUND rid=2 spine=1 page=0 kind=x']
    assert vi_tri(lines) == (1, 0) and vi_tri([]) is None
    nho = {}
    assert su_kien('vi-tri', lines[:1], None, nho)[0] and nho['dau'] == (0, 3)
    assert su_kien('tien', lines, None, nho)[0]
    assert not su_kien('ve-cho-cu', lines, None, nho)[0]
    assert su_kien('ve-cho-cu', lines[:1], None, nho)[0]
    assert not su_kien('xam', ['PAINT_GRAY_DONE'] * 6 + ['[ERS] GRAY_SKIPPED heap_floor free=1'], None, {})[0]
    assert su_kien('xam', ['PAINT_GRAY_DONE'] * 6, None, {})[0]
    assert not su_kien('hop-co-chu', [], Image.new('1', (100, 100), 1), {}, {'den': 10})[0]
    assert not su_kien('dung-lai', ['CLEAR_BOOK_CACHE:/x,removed=1', 'Failed to stream item'] + lines, None, {})[0]
    print('tu kiem: ok')


if __name__ == '__main__':
    if sys.argv[1:] == ['--tu-kiem']:
        tu_kiem(); sys.exit(0)
    a = sys.argv
    opt = lambda k: a[a.index(k) + 1] if k in a else None
    chon = opt('--chang').split(',') if opt('--chang') else None
    rows = chay(a[1], a[2], chon, opt('--moc'), opt('--spike'))
    du = chon is None or len(rows) == len(chon)
    sys.exit(0 if du and rows and all(r['trang'] == 'XANH' for r in rows) else 1)
