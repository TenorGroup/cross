"""X4 Pro dynamic bar: one bar at the foot of every screen, what it holds declared by the screen.

A screen below another holds "<" (x 16-76), the zone's round icon (x 84-144: a tap leads to the zone's
root) and the screen's name (from x 152, read only). The top of the screen is the status strip alone.
Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import tempfile
import zipfile
from pathlib import Path

from test_thanh_day import run, ink, BAR_TOP, TABS_X

BAR_Y = BAR_TOP + 30
ROW = [66 + 62 * i for i in range(10)]   # row centres of the Settings card under the status strip
SYSTEM = 5                               # "He thong" on the Settings card
SAME = (0, 30, 480, 720)                 # everything under the clock


def fresh(tmp, name):
    folder = Path(tmp) / name
    folder.mkdir()
    return folder


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
    # A book -> its menu -> Vi tri -> Chon chuong -> the zone icon (an open book): the page, not the menu.
    page, contents, back = run(fresh(tmp, 'bz'), '3000:TAP:240,300;7000:TAP:240,400;9500:TAP:180,754;'
                                                  '12500:TAP:240,134;15500:TAP:114,754', [6800, 15000, 18000])
    assert ink(contents, (84, 724, 144, 784)) > 0.08, 'the contents have no zone icon'
    assert same(page, back, (0, 0, 480, 700)), 'the zone icon of the contents did not lead to the page'


def check_reader_menu_back(tmp):
    # The reader menu: "<" at the left of its bar, its cards right of it; "<" closes the menu to the page.
    page, menu, back = run(fresh(tmp, 'rm'), '3000:TAP:240,300;7000:TAP:240,400;9500:TAP:46,754', [6800, 9300, 12000])
    assert ink(menu, (16, 724, 76, 784)) > 0.04, 'the reader menu has no "<" at the foot'
    assert same(page, back, (0, 0, 480, 700)), '"<" in the reader menu did not close it to the page'


def check_keyboard_back(tmp):
    # Settings -> Gui file -> Ket noi mang -> Them mang an...: the keyboard; "<" closes it.
    kb, after = run(fresh(tmp, 'kb'), f'3000:TAP:{TABS_X[4]},{BAR_Y};5000:TAP:240,66;7500:TAP:240,70;'
                                      '10500:TAP:240,160;13000:TAP:46,754', [12800, 15500])
    assert ink(kb, (16, 724, 76, 784)) > 0.04, 'the keyboard screen has no "<"'
    assert not same(kb, after, (0, 30, 480, 716)), '"<" on the keyboard did not go back'


def check_chosen_row(tmp):
    # The chapter being read is the chosen row of the contents: a tick at its end, no "Dang doc".
    open_book = f'3000:TAP:{TABS_X[1]},{BAR_Y};5000:TAP:240,68;7000:TAP:240,68'
    (contents,) = run(fresh(tmp, 'ch'), f'{open_book};9500:TAP:240,400;12000:TAP:180,754;14500:TAP:240,134',
                      [17000], write_books=toc_book)
    assert ink(contents, (420, 45, 450, 95)) > 0.03, 'no tick at the end of the chapter being read'
    assert ink(contents, (250, 45, 415, 95)) == 0, 'the chapter being read still says so in words'


def check_row_chevron(tmp):
    # A row that opens a deeper screen ends with a grey ">" (Dong ho), a row changed in place does not
    # (Hien file an), and its value stands left of where the ">" goes.
    card, group = run(fresh(tmp, 'rc'), f'3000:TAP:{TABS_X[4]},{BAR_Y};5000:TAP:240,{ROW[SYSTEM]}', [4800, 7800])
    assert ink(card, (436, 40, 452, 600)) > 0.01, 'the Settings card rows have no ">"'
    assert ink(group, (436, 40, 452, 100)) > 0.02, 'Dong ho has no ">"'
    assert ink(group, (436, 104, 456, 160)) == 0, 'a row changed in place has a ">" or its value under it'


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
    # The contents open on the page that holds the chapter being read, pages counted from the first line:
    # read chapter 15 (page 2, its fourth line), open the contents again, see page 2 as a flick showed it.
    contents = '9500:TAP:240,400;12000:TAP:180,754;14500:TAP:240,134'
    open_book = f'3000:TAP:{TABS_X[1]},{BAR_Y};5000:TAP:240,68;7000:TAP:240,68'
    page2, again = run(fresh(tmp, 'cp'), f'{open_book};{contents};17500:SWIPE:240,600,240,200,150;'
                       '20500:TAP:240,254;' + contents.replace('9500', '24000').replace('12000', '26500')
                       .replace('14500', '29000'), [20000, 32000], write_books=toc_book)
    # The first rows only: the chapter now read (further down) is bold.
    assert same(page2, again, (0, 30, 300, 220)), 'the contents opened on a page of their own'


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-dyn-') as tmp:
        failed = 0
        for check in (check_settings_row_tap, check_sub_screen_bar, check_zone_tap, check_book_zone, check_back_keeps_list, check_contents_page, check_file_root, check_status_strip, check_row_chevron, check_chosen_row, check_reader_menu_back, check_keyboard_back):
            try:
                check(tmp)
                print('ok   ', check.__name__)
            except AssertionError as error:
                failed += 1
                print('RED  ', check.__name__, '-', error)
    print('RED' if failed else 'GREEN: X4 Pro dynamic bar')


if __name__ == '__main__':
    main()
