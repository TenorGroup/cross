"""Reader menu on the simulators: the sheet stands on the bar's edges, has no grabber, its chevrons are the bar's
chevron, its ring is the group ring, and the reader's bar is the tab list's bar.

  run.py --x3 <simulator_x3_uc8279 program> --x4 <simulator_x4pro program> [--out DIR]

Pixels come from real screenshots (528 x 792 X3, 480 x 800 X4 Pro); nothing is read from the source.
"""
import argparse
import json
import os
import shutil
import subprocess
import tempfile
import zipfile
from pathlib import Path

import numpy as np
from scipy import ndimage
from PIL import Image

REPO = Path(__file__).resolve().parents[2]


def make_book(path, chapters=24):
    head = '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>T</title></head><body>'
    with zipfile.ZipFile(path, 'w') as z:
        z.writestr(zipfile.ZipInfo('mimetype'), 'application/epub+zip')
        z.writestr('META-INF/container.xml', '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        for i in range(1, chapters + 1):
            body = ''.join(f'<p>Doan {n:02d} cua chuong {i}. Nguoi doc giu nguyen vi tri khi menu mo ra va dong lai. </p>' for n in range(30))
            z.writestr(f'c{i}.xhtml', f'{head}<h1 id="c{i}">Chuong {i}</h1>{body}</body></html>')
        nav = ''.join(f'<navPoint id="n{i}" playOrder="{i}"><navLabel><text>Chuong {i}</text></navLabel><content src="c{i}.xhtml#c{i}"/></navPoint>' for i in range(1, chapters + 1))
        z.writestr('toc.ncx', f'<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><head/><docTitle><text>T</text></docTitle><navMap>{nav}</navMap></ncx>')
        items = ''.join(f'<item id="c{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>' for i in range(1, chapters + 1))
        spine = ''.join(f'<itemref idref="c{i}"/>' for i in range(1, chapters + 1))
        z.writestr('book.opf', f'<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>T</dc:title><dc:identifier id="id">rmu</dc:identifier><dc:language>vi</dc:language></metadata><manifest>{items}<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest><spine toc="ncx">{spine}</spine></package>')


def run(program, work, keys, shots, tier=0, style=1, home=False, ugly=False, port=29600):
    """keys: [(ms, 'CONFIRM' | 'TAP:x,y' | ...)]; shots: {ms: name}. Returns {name: ink array (True = ink)}."""
    sd = work / 'sd'
    (sd / '.crosspoint').mkdir(parents=True)
    make_book(sd / 'sach.epub')
    settings = {'language': 'VI', 'uiShell': 1 if ugly else 0, 'uiTheme': 4, 'uiTextSize': tier, 'readerMenuStyle': style,
                'wakeIntoBook': 0 if home else 1, 'fontSize': 18, 'fontFamily': 0, 'textSpacingVersion': 3,
                'paragraphIndentVersion': 1, 'readerInkWeightVersion': 1, 'tenorPresetVersion': 1, 'uiShellSleepMemo': 0,
                'sleepTimeout': 10, 'readerTapTip': 0, 'tipsEnabled': 1, 'tenorButtonSymbols': 1, 'tenorSideArrows': 1,
                'readerFavoritesDaDat': 1, 'readerFavoriteCount': 0, 'sdFontFamilyName': ''}
    (sd / '.crosspoint/settings.json').write_text(json.dumps(settings))
    (sd / '.crosspoint/state.json').write_text(json.dumps({'showBootScreen': False, 'openEpubPath': '' if home else '/sach.epub', 'lastSleepFromReader': not home}))
    (sd / '.crosspoint/recent.json').write_text(json.dumps({'books': [{'path': '/sach.epub', 'title': 'T'}]}))
    env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
    env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_WAKE_REASON='power', CROSSPOINT_SIM_HTTP_PORT=str(port),
               CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(f'{ms}:{k}' for ms, k in keys) + f';{max(shots) + 700}:QUIT',
               CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{work}/{n}.bmp' for ms, n in shots.items()))
    p = subprocess.run([str(program)], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
    assert p.returncode == 0, (p.stdout + p.stderr)[-1500:]
    out = {}
    for n in shots.values():
        out[n] = np.array(Image.open(work / f'{n}.bmp').convert('L')) < 128
    return out


def dotted_cols(ink, y0, y1):
    """Left columns of 2 px dotted vertical lines (each column half ink, the pair filling every row)."""
    band, n, found = ink[y0:y1], y1 - y0, []
    for x in range(ink.shape[1] - 1):
        a, b = band[:, x], band[:, x + 1]
        if abs(a.sum() / n - .5) < .12 and abs(b.sum() / n - .5) < .12 and (a | b).sum() / n > .9 and (a & b).sum() / n < .1:
            found.append(x)
    return found


def bbox(ink, x0, y0, x1, y1):
    ys, xs = np.where(ink[y0:y1, x0:x1])
    return (x0 + xs.min(), y0 + ys.min(), x0 + xs.max(), y0 + ys.max()) if len(xs) else None


def biggest_part(ink, x0, y0, x1, y1):
    """The widest box of a connected ink part in the window: a row end apart from the dotted rules around it."""
    labels, count = ndimage.label(ink[y0:y1, x0:x1], structure=np.ones((3, 3)))
    if not count:
        return None
    boxes = ndimage.find_objects(labels)
    ys, xs = max(boxes, key=lambda b: (b[0].stop - b[0].start) * (b[1].stop - b[1].start))
    return (x0 + xs.start, y0 + ys.start, x0 + xs.stop - 1, y0 + ys.stop - 1)


def long_runs(ink, y, minimum):
    r, s = [], None
    for x in range(ink.shape[1] + 1):
        v = x < ink.shape[1] and ink[y, x]
        if v and s is None:
            s = x
        if not v and s is not None:
            if x - s >= minimum:
                r.append((s, x - 1))
            s = None
    return r


def x3(program, tmp, fails):
    # CONFIRM opens the toolbar; CONFIRM again the Position panel; DOWN x3 reaches Tools (panel), RIGHT x4 from the
    # toolbar focuses the last tab (Text) for the bar's right end.
    a = run(program, tmp / 'a', [(2000, 'CONFIRM'), (3500, 'CONFIRM'), (5200, 'DOWN'), (7000, 'DOWN'), (8800, 'UP'), (10600, 'UP'), (12400, 'UP')],
            {3200: 'toolbar', 5000: 'position', 6800: 'reading', 8600: 'tools', 14200: 'favorites'})
    b = run(program, tmp / 'b', [(2000, 'CONFIRM'), (3200, 'RIGHT'), (4200, 'RIGHT'), (5200, 'RIGHT')], {6200: 'text-focus'}, port=29601)
    # The bar's ring: the first tab chosen (Favorites) and the last (Text); their outer columns are the sheet's edges.
    first = bbox(a['favorites'], 0, 708, 120, 770)
    last = bbox(b['text-focus'], 400, 708, 528, 770)
    left, right = first[0], last[2]
    x3.edges = (left, right)
    for name in ('toolbar', 'position', 'reading', 'tools'):
        cols = dotted_cols(a[name], 625 if name == 'toolbar' else 520, 675 if name == 'toolbar' else 600)
        if not cols or cols[0] != left or cols[-1] + 1 != right:
            fails.append(f'X3 {name}: sheet edges {cols[:1]}..{cols[-1:]} ({"+1" if cols else ""}) but bar ring {left}..{right}')
    # No grabber: no black run 60 to 90 px long (the grabber is 72 x 5) inside the sheet above the bar.
    for name in ('toolbar', 'position', 'reading', 'tools'):
        rows = [y for y in range(380, 700) if any(60 <= e - s + 1 <= 90 for s, e in long_runs(a[name][:, left + 24:right - 24], y, 60))]
        if rows:
            fails.append(f'X3 {name}: a grabber-like run of ink at rows {rows[:3]}')
    # Chevron: the toolbar's chapter step is the 10 x 19 stroke-3 chevron of the bar's "<", centred on the track.
    t = a['toolbar']
    prev = bbox(t, 24, 606, 70, 650)
    track = [y for y in range(600, 655) if t[y, 90:440].all()]
    if prev is None or (prev[2] - prev[0] + 1, prev[3] - prev[1] + 1) != (10, 19):
        fails.append(f'X3 step chevron box {prev}: want 10 x 19')
    elif track and abs((prev[1] + prev[3]) / 2 - sum(track) / len(track)) > 0.5:
        fails.append(f'X3 step chevron centre {(prev[1] + prev[3]) / 2} but track {sum(track) / len(track)}')
    # A row ends by what it does: the night mode row is a 46 x 26 switch, the orientation row ends in the up-down mark.
    reading = a['reading']
    top = next(y for y in range(380, 700) if long_runs(reading, y, 200))
    switch = biggest_part(reading, 380, top + 4, 500, top + 48)
    if switch is None or (switch[2] - switch[0] + 1, switch[3] - switch[1] + 1) != (46, 26):
        fails.append(f'X3 night mode row end {switch}: want a 46 x 26 switch')
    mark = bbox(reading, 462, top + 112 + 8, 490, top + 112 + 44)
    if mark is None or (mark[2] - mark[0] + 1) != 13 or not 10 <= mark[3] - mark[1] + 1 <= 16:
        fails.append(f'X3 orientation row end {mark}: want the 13 px wide up-down mark')
    # Both end on the list's trailing column (listChevronInkRight), as every row end does.
    if switch and mark and switch[2] != mark[2]:
        fails.append(f'X3 row ends: switch ends at {switch[2]}, up-down mark at {mark[2]}: want one column')
    # The ring is the group ring all round: the bottom edge is a dotted row pair too, and no side column is solid.
    for name in ('position', 'tools'):
        cols = [x for x in (left, left + 1, right - 2, right - 1) if a[name][480:560, x].sum() > 0.8 * 80]
        if cols:
            fails.append(f'X3 {name}: solid black side column at {cols}, the ring is grey dots')


def dotted_rows(ink, x0, x1, y0=0, y1=None):
    """Top rows of 2 px dotted horizontal lines (each row half ink, the pair filling every column)."""
    return [y for y in range(y0, (y1 or ink.shape[0]) - 1)
            if all(abs(r[x0:x1].sum() / (x1 - x0) - .5) < .12 for r in (ink[y], ink[y + 1]))
            and (ink[y, x0:x1] | ink[y + 1, x0:x1]).sum() / (x1 - x0) > .9 and (ink[y, x0:x1] & ink[y + 1, x0:x1]).sum() / (x1 - x0) < .1]


def rule_rows(ink, x0, x1, y0, y1):
    """Rows that are a 1 px grey dotted rule: every (x + y) even pixel inked, no other."""
    rows = []
    for y in range(y0, y1):
        seg = ink[y, x0:x1]
        even = np.array([(x + y) % 2 == 0 for x in range(x0, x1)])
        if seg[even].mean() > .95 and seg[~even].mean() < .03:
            rows.append(y)
    return rows


def x3_lists(program, tmp, fails):
    """The lists of the reader menu stand in the group frame on the tab bar's edges (the X3's Danh sach, its Chon chuong and
    Cai dat van ban), the progress line stands over the bar, the heart of a pinned row after its name, the edge arrow
    stays readable beside the scroll bar."""
    left, right = x3.edges
    lists = run(program, tmp / 'l', [(2000, 'CONFIRM'), (3500, 'DOWN'), (5000, 'DOWN'), (6500, 'DOWN')], {7800: 'tools'}, style=0)
    chapters = run(program, tmp / 'c', [(2000, 'CONFIRM'), (3500, 'CONFIRM'), (5000, 'CONFIRM')], {6500: 'chapters'}, port=29601)
    text = run(program, tmp / 't', [(2000, 'CONFIRM'), (3500, 'DOWN'), (5000, 'DOWN'), (6500, 'DOWN'), (8000, 'DOWN'),
                                      (9500, 'CONFIRM'), (11000, 'CONFIRM')], {12500: 'text'}, style=0, port=29602)
    for name, shot, band in (('list', lists['tools'], (100, 400)), ('chapters', chapters['chapters'], (100, 400)),
                             ('text settings', text['text'], (300, 560))):
        cols = dotted_cols(shot, *band)
        if not cols or cols[0] != left or cols[-1] + 1 != right:
            fails.append(f'X3 {name}: frame edges {cols[:1]}..{cols[-1:]} but bar ring {left}..{right}')
        rules = rule_rows(shot, left + 40, right - 40, 40, 690)
        if len(rules) < 2:
            fails.append(f'X3 {name}: {len(rules)} grey dotted rules between rows, want one under each row')
    tools = lists['tools']
    over = int(tools[648:690, 100:430].sum())
    if over < 30:
        fails.append(f'X3 list: the progress line is not over the bar (ink {over} in the band)')
    heart = bbox(tools, 218, 64, 262, 88)
    if heart is None:
        fails.append('X3 list: no heart after the name of the pinned row')
    white = int((~chapters['chapters'][188:204, 517:523]).sum())
    if white < 6:
        fails.append(f'X3 chapters: the scroll bar hides the right edge arrow ({white} white pixels over the thumb)')


def x4(program, tmp, fails):
    keys = [(2500, 'CONFIRM'), (4500, 'TAP:136,754'), (6500, 'TAP:228,754'), (8500, 'TAP:412,754')]
    a = run(program, tmp / 'a', keys, {3600: 'toolbar', 5600: 'tool0', 7600: 'tool1', 9600: 'tool3'})
    b = run(program, tmp / 'b', [(2500, 'CONFIRM'), (4500, 'TAP:320,754'), (6500, 'TAP:136,754'), (8500, 'TAP:228,754'), (10500, 'TAP:412,754')],
            {6000: 'layout', 7600: 'tab0', 9600: 'tab1', 11600: 'tab3'}, port=29601)
    # The bar the reader draws is the bar its text settings draw: same pixels outside the icons.
    for tool, tab, centre in (('tool0', 'tab0', 134), ('tool1', 'tab1', 228), ('tool3', 'tab3', 413)):
        x, y = a[tool][724:784].copy(), b[tab][724:784].copy()
        for c in (134, 228, 320, 413):
            x[10:50, c - 22:c + 22] = False
            y[10:50, c - 22:c + 22] = False
        diff = int((x != y).sum())
        if diff:
            fails.append(f'X4 {tool}: {diff} pixels of the reader bar differ from the text settings bar')
    more = run(program, tmp / 'c', [(2500, 'CONFIRM'), (4500, 'TAP:412,754')], {6000: 'more'}, port=29602)['more']
    switch = bbox(more, 340, 470, 462, 520)
    if switch is None or (switch[2] - switch[0] + 1, switch[3] - switch[1] + 1) != (46, 26):
        fails.append(f'X4 night mode row end {switch}: want a 46 x 26 switch')
    # One constant of air between a frame and the bar (8 px), and the chevron column of the sheet is the settings list's.
    rows = dotted_rows(a['tool0'], 100, 380, 690, 730)
    if 714 not in rows:
        fails.append(f'X4 sheet foot rows {rows}: want its ring on rows 714 and 715, 8 px over the bar')
    chevron = bbox(a['tool0'], 441, 486, 456, 520)
    layout = bbox(b['layout'], 441, 548, 456, 588)
    if chevron is None or layout is None or chevron[2] != layout[2]:
        fails.append(f'X4 chevron column: sheet {chevron and chevron[2]}, text settings {layout and layout[2]}')
    spacing = run(program, tmp / 'd', [(2500, 'CONFIRM'), (4500, 'TAP:320,754'), (6500, 'TAP:240,561')], {8200: 'spacing'}, port=29603)['spacing']
    columns = spacing[540:636].any(axis=0)
    marks = [64 + 351 * i // 6 for i in range(7)]
    touching = [i for i in range(6) if columns[(marks[i] + marks[i + 1]) // 2]]
    if touching:
        fails.append(f'X4 line spacing ruler: the labels of marks {touching} touch the next one')
    t = a['toolbar']
    prev = bbox(t, 30, 604, 80, 664)
    if prev is None or (prev[2] - prev[0] + 1, prev[3] - prev[1] + 1) != (10, 19):
        fails.append(f'X4 step chevron box {prev}: want 10 x 19')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--x3', type=Path)
    parser.add_argument('--x4', type=Path)
    args = parser.parse_args()
    fails = []
    with tempfile.TemporaryDirectory(prefix='readermenu-') as tmp:
        tmp = Path(tmp)
        if args.x3:
            x3(args.x3.resolve(), tmp / 'x3', fails)
            x3_lists(args.x3.resolve(), tmp / 'x3b', fails)
        if args.x4:
            x4(args.x4.resolve(), tmp / 'x4', fails)
    for line in fails:
        print('RED:', line)
    if fails:
        raise SystemExit(1)
    print('GREEN: reader menu sheet edges, no grabber, bar chevrons, group ring, same bar as the text settings')


if __name__ == '__main__':
    main()
