"""X4 Pro dynamic bar: one bar at the foot of every screen, what it holds declared by the screen.

A screen below another holds "<" (x 16-76), the zone's round icon (x 84-144: a tap leads to the zone's
root) and the screen's name (from x 152, read only). The top of the screen is the status strip alone.
Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import tempfile
import os
import re
import zipfile
from pathlib import Path

from test_thanh_day import run, ink, BAR_TOP, TABS_X

BAR_Y = BAR_TOP + 30
ROW = [70, 132, 194, 256, 336, 398, 460, 522, 584]  # 2 grouped Settings frames, default tier
SYSTEM = 5                               # "He thong" on the Settings card
SAME = (0, 30, 480, 720)                 # everything under the clock


def fresh(tmp, name):
    folder = Path(tmp) / name
    folder.mkdir()
    return folder


def capture(folder, names, images):
    output = os.environ.get('X4PRO_TEST_SHOTS')
    if output:
        target = Path(output) / folder.name
        target.mkdir(parents=True, exist_ok=True)
        (target/'simulator.log').write_text((folder/'simulator.log').read_text())
        for name, image in zip(names, images): image.save(target/(name+'.png'))


def require_reader(folder):
    assert 'Entering activity: EpubReader' in (folder/'simulator.log').read_text(), 'fixture never entered Reader'


def same(a, b, box=SAME):
    return list(a.crop(box).getdata()) == list(b.crop(box).getdata())


def check_settings_row_tap(tmp):
    # Settings -> He thong -> a tap on its first row (Dong ho) opens it.
    group, clock = run(fresh(tmp, 'rt'), f'3000:TAP:{TABS_X[4]},{BAR_Y};5000:TAP:240,{ROW[SYSTEM]};8000:TAP:240,68',
                       [7800, 11000])
    assert not same(group, clock), 'a tap on a settings row did nothing'


def check_sub_screen_bar(tmp):
    # A settings group: "<", the zone icon and the name pill at the foot; no title or sibling line on top.
    (group,) = run(fresh(tmp, 'sb'), f'3000:TAP:{TABS_X[4]},{BAR_Y};5000:TAP:240,{ROW[SYSTEM]}', [7800])
    assert ink(group, (16, 724, 76, 784)) > 0.04, 'no "<" at the foot'
    assert ink(group, (84, 724, 144, 784)) > 0.08, 'no zone icon at the foot'
    assert ink(group, (152, 724, 300, 784)) > 0.04, 'no name pill at the foot'
    assert ink(group, (90, 0, 330, 24)) == 0, 'a title is still drawn at the top'  # the clock is at the left


def check_zone_tap(tmp):
    # He thong -> Dong ho -> the zone icon: back on the Settings card, two levels up in one tap.
    card, root = run(fresh(tmp, 'zt'), f'3000:TAP:{TABS_X[4]},{BAR_Y};5000:TAP:240,{ROW[SYSTEM]};'
                                       '8000:TAP:240,68;11000:TAP:114,754', [4800, 13500])
    assert same(card, root), 'the zone icon did not lead to the Settings card'


def check_back_keeps_list(tmp):
    # Settings card -> Khac (the last row) -> "<": the card comes back as it was, not scrolled.
    card, back = run(fresh(tmp, 'bl'), f'3000:TAP:{TABS_X[4]},{BAR_Y};5000:TAP:240,{ROW[8]};8000:TAP:46,754',
                     [4800, 10500])
    assert same(card, back), 'the Settings card came back scrolled'


def check_book_zone(tmp):
    # Inline Contents uses the common reader tool bar; foot Back returns to the book page.
    open_book = f'3000:TAP:{TABS_X[1]},{BAR_Y};5000:TAP:240,68;7000:TAP:240,68'
    folder = fresh(tmp, 'bz')
    page, contents, back = run(folder, f'{open_book};9500:TAP:240,775;'
                              '12000:TAP:148,754;15500:TAP:46,754',
                              [9000,14500,18000], write_books=toc_book)
    require_reader(folder)
    capture(folder, ('page','contents','back'), (page,contents,back))
    assert not same(page, contents, (16,364,464,710)), 'Contents never opened'
    assert ink(contents, (32,370,160,398)) > 0.04, 'Contents sheet has no heading'
    assert ink(contents, (128,730,168,774)) > 0.08, 'Contents tool is missing from the reader bar'
    assert same(page, back, (0,0,480,700)), 'foot Back from Contents did not lead to the book page'


def check_reader_menu_back(tmp):
    # The reader menu: "<" at the left of its bar, its cards right of it; "<" closes the menu to the page.
    page, menu, back = run(fresh(tmp, 'rm'), '3000:TAP:240,300;7000:TAP:240,775;9500:TAP:46,754', [6800, 9300, 12000])
    assert ink(menu, (16, 724, 76, 784)) > 0.04, 'the reader menu has no "<" at the foot'
    assert same(page, back, (0, 0, 480, 700)), '"<" in the reader menu did not close it to the page'


def check_keyboard_back(tmp):
    # Settings -> Gui file -> Ket noi mang -> Them mang an...: the keyboard; "<" closes it.
    kb, after = run(fresh(tmp, 'kb'), f'3000:TAP:{TABS_X[4]},{BAR_Y};5000:TAP:240,{ROW[4]};7500:TAP:240,70;'
                                      '10500:TAP:240,160;13000:TAP:46,754', [12800, 15500])
    assert ink(kb, (16, 724, 76, 784)) > 0.04, 'the keyboard screen has no "<"'
    assert not same(kb, after, (0, 30, 480, 716)), '"<" on the keyboard did not go back'


def check_chosen_row(tmp):
    # The chapter being read is the chosen row of the contents: a tick at its end, no "Dang doc".
    open_book = f'3000:TAP:{TABS_X[1]},{BAR_Y};5000:TAP:240,68;7000:TAP:240,68'
    folder = fresh(tmp, 'ch')
    (contents,) = run(folder, f'{open_book};9500:TAP:240,775;12000:TAP:148,754',
                      [17000], write_books=toc_book)
    require_reader(folder)
    capture(folder, ('contents',), (contents,))
    assert ink(contents, (32,370,160,398)) > 0.04, 'Contents never opened'
    assert ink(contents, (416,408,448,458)) > 0.03, 'no tick at the end of the chapter being read'
    assert ink(contents, (300,408,416,458)) == 0, 'the chapter being read still says so in words'


def check_row_chevron(tmp):
    # A row that opens a deeper screen ends with a grey ">" (Dong ho), a row changed in place does not
    # (Hien file an), and its value stands left of where the ">" goes.
    card, group = run(fresh(tmp, 'rc'), f'3000:TAP:{TABS_X[4]},{BAR_Y};5000:TAP:240,{ROW[SYSTEM]}', [4800, 7800])
    assert ink(card, (436, 40, 452, 600)) > 0.01, 'the Settings card rows have no ">"'
    assert ink(group, (436, 40, 452, 100)) > 0.02, 'Dong ho has no ">"'
    # The actual first-row chevron occupies x437..443. The old crop included x436,
    # where TAT ends: two pixels belonging to the last T, outside that corridor.
    # Row2 interior is y112..172 since every framed row has one height: y111 and y173 are the dotted
    # separators, not a chevron.
    assert ink(group, (400,112,437,173)) > 0.02, 'toggle value is missing'
    assert ink(group, (437,112,444,173)) == 0, 'a row changed in place has a ">" or its value under it'


def check_status_strip(tmp):
    # The strip at the top (rule 10): the clock at the left, the battery at the right, and in a folder the
    # count of its items in the middle.
    (folder,) = run(fresh(tmp, 'ss'), f'3000:TAP:{TABS_X[1]},{BAR_Y};5000:TAP:240,68', [7500], extra_books=4)
    assert ink(folder, (14, 0, 80, 24)) > 0.04, 'no clock at the left of the strip'
    assert ink(folder, (420, 0, 476, 24)) > 0.04, 'no battery at the right of the strip'
    assert ink(folder, (190, 0, 290, 24)) > 0.02, 'no item count in the middle of the strip'


def check_file_root(tmp):
    # The File card is the root of File: its rows have the folder icon of a folder's rows, and "<" from a
    # folder right under it comes back to the card, not to a second root screen.
    card, folder, back = run(fresh(tmp, 'fr'), f'3000:TAP:{TABS_X[1]},{BAR_Y};5000:TAP:240,68;8000:TAP:46,754',
                             [4800, 7500, 10500])
    assert ink(card, (30, 45, 64, 95)) > 0.05, 'the File card rows have no icon'
    assert not same(card, folder), 'the folder did not open'
    assert same(card, back), '"<" from a folder under the root did not come back to the File card'


def toc_book(sach, chapters=24):
    """a-muc-luc.epub: one short chapter per contents line, first in the folder."""
    head = '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>T</title></head><body>'
    with zipfile.ZipFile(sach / 'a-muc-luc.epub', 'w') as z:
        z.writestr(zipfile.ZipInfo('mimetype'), 'application/epub+zip')
        z.writestr('META-INF/container.xml', '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:'
                   'xmlns:container" version="1.0"><rootfiles><rootfile full-path="book.opf" '
                   'media-type="application/oebps-package+xml"/></rootfiles></container>')
        for i in range(1, chapters + 1):
            z.writestr(f'c{i}.xhtml', f'{head}<h1 id="c{i}">Chuong {i}</h1><p>Mot doan van ngan.</p></body></html>')
        nav = ''.join(f'<navPoint id="n{i}" playOrder="{i}"><navLabel><text>Chuong {i}</text></navLabel>'
                      f'<content src="c{i}.xhtml#c{i}"/></navPoint>' for i in range(1, chapters + 1))
        z.writestr('toc.ncx', '<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">'
                   f'<head/><docTitle><text>Muc luc</text></docTitle><navMap>{nav}</navMap></ncx>')
        items = ''.join(f'<item id="c{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>'
                        for i in range(1, chapters + 1))
        spine = ''.join(f'<itemref idref="c{i}"/>' for i in range(1, chapters + 1))
        z.writestr('book.opf', '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" '
                   'unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Muc luc'
                   '</dc:title><dc:identifier id="id">toc-24</dc:identifier><dc:language>vi</dc:language></metadata>'
                   f'<manifest>{items}<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest>'
                   f'<spine toc="ncx">{spine}</spine></package>')


def check_contents_page(tmp):
    # Five rows fit the approved sheet. Two swipes reach chapters 11..15; tap 15,
    # then open again: current chapter is the first row and carries the chosen tick.
    folder = fresh(tmp, 'cp')
    open_book = f'3000:TAP:{TABS_X[1]},{BAR_Y};5000:TAP:240,68;7000:TAP:240,68'
    page3, chapter15, again = run(folder, f'{open_book};9500:TAP:240,775;12000:TAP:148,754;'
        '14500:SWIPE:240,650,240,420,150;17000:SWIPE:240,650,240,420,150;'
        '19500:TAP:240,681;22500:TAP:240,775;25000:TAP:148,754',
        [19000,21500,27500], write_books=toc_book)
    require_reader(folder)
    capture(folder, ('chapters-11-15','chapter15','current-chapter-contents'), (page3,chapter15,again))
    assert 'Progress saved: spine=14 ' in (folder/'simulator.log').read_text(), 'fixture did not read chapter 15'
    assert ink(page3,(32,370,160,398)) > 0.04 and ink(again,(32,370,160,398)) > 0.04, 'Contents never opened'
    assert ink(again,(416,408,448,458)) > 0.03, 'current chapter did not open as the chosen first row'
    # Chapter 15 was row 5; reopening must move the viewport to its chosen first row.
    assert not same(page3,again,(32,404,410,466)), 'Contents ignored current chapter and reused old viewport'


def toc_without_current(sach, empty=False):
    toc_book(sach,2)
    path=sach/'a-muc-luc.epub'
    with zipfile.ZipFile(path) as z: parts={n:z.read(n) for n in z.namelist()}
    ncx=parts['toc.ncx'].decode()
    ncx=re.sub(r'<navPoint[^>]*>.*?</navPoint>', '' if empty else lambda m: '' if 'id="n1"' in m.group() else m.group(), ncx)
    parts['toc.ncx']=ncx.encode()
    with zipfile.ZipFile(path,'w') as z:
        for name,data in parts.items(): z.writestr(name,data)


def check_contents_unmatched(tmp):
    for empty in (False,True):
        folder=fresh(tmp,'toc-empty' if empty else 'toc-unmatched')
        (contents,)=run(folder,f'3000:TAP:{TABS_X[1]},{BAR_Y};5000:TAP:240,68;7000:TAP:240,68;'
            '9500:TAP:240,775;12000:TAP:148,754',[14500],write_books=lambda sach: toc_without_current(sach,empty))
        require_reader(folder)
        capture(folder,('contents',),(contents,))
        assert ink(contents,(32,370,160,398)) > 0.04, 'empty/unmatched fixture never opened Contents'
        for row in range(5):
            # Tick slots stay inside the five fixed rows, above the frame border at y708.
            top=408+62*row
            assert ink(contents,(416,top,448,top+50)) == 0, 'empty/unmatched TOC marked an unrelated row'


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-dyn-') as tmp:
        failed = 0
        for check in (check_settings_row_tap, check_sub_screen_bar, check_zone_tap, check_book_zone, check_back_keeps_list, check_contents_page, check_file_root, check_status_strip, check_row_chevron, check_chosen_row, check_reader_menu_back, check_keyboard_back, check_contents_unmatched):
            try:
                check(tmp)
                print('ok   ', check.__name__)
            except AssertionError as error:
                failed += 1
                print('RED  ', check.__name__, '-', error)
    assert not failed, f'{failed} dynamic bar checks failed'
    print('GREEN: X4 Pro dynamic bar')


if __name__ == '__main__':
    main()
