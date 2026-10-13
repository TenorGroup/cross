#!/usr/bin/env python3
"""X4 Pro reader menu Favorites: a held row offers Unpin and Reorder; on the order screen a tap moves the held row,
Done keeps the order (back on Favorites) and it is still there after a restart, "<" leaves it as it was.

x4_reader_favorite_order.py <simulator_x4pro program> <output dir>
RED on v1.0.58: a hold unpinned the row at once, so the X4 Pro could not reorder the reader's Favorites (the X3 list
menu holds Left or Right).
"""
import argparse
import io
import json
import sys
import zipfile
from pathlib import Path

from PIL import ImageChops

sys.path.insert(0, str(Path(__file__).parent))
import sim

ap = argparse.ArgumentParser()
ap.add_argument('program')
ap.add_argument('output', type=Path)
ap.add_argument('--port', type=int, default=30841)
a = ap.parse_args()
out = a.output.resolve()
PINS = [14, 'text/fontSize', 'text/letterSpacing']
SHEET = (0, 360, 480, 720)
OPEN_FAVORITES = [('00-file', 'TAP:150,754'), ('01-book', 'TAP:240,68'), ('02-menu', 'TAP:240,775'),
                  ('03-fav', 'TAP:131,754')]
HOLD_FIRST = 'TAP:240,433,900'
UNPIN, REORDER = 'TAP:75,508', 'TAP:75,565'  # the menu under the held row
LAST_ROW = 'TAP:240,204'        # the 3rd of 3 rows on the order screen
DONE, BACK = 'TAP:420,754', 'TAP:46,754'


def book():
    head = '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>T</title></head><body>'
    data = io.BytesIO()
    with zipfile.ZipFile(data, 'w') as z:
        z.writestr(zipfile.ZipInfo('mimetype'), 'application/epub+zip')
        z.writestr('META-INF/container.xml', '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:'
                   'xmlns:container" version="1.0"><rootfiles><rootfile full-path="book.opf" '
                   'media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('c1.xhtml', f'{head}<h1 id="c1">Chuong 1</h1>' + '<p>Mot doan van ngan.</p>' * 30 + '</body></html>')
        z.writestr('toc.ncx', '<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">'
                   '<head/><docTitle><text>Sach</text></docTitle><navMap><navPoint id="n1" playOrder="1"><navLabel>'
                   '<text>Chuong 1</text></navLabel><content src="c1.xhtml#c1"/></navPoint></navMap></ncx>')
        z.writestr('book.opf', '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" '
                   'unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Sach'
                   '</dc:title><dc:identifier id="id">s1</dc:identifier><dc:language>vi</dc:language></metadata>'
                   '<manifest><item id="c1" href="c1.xhtml" media-type="application/xhtml+xml"/><item id="ncx" '
                   'href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest><spine toc="ncx">'
                   '<itemref idref="c1"/></spine></package>')
    return data.getvalue()


def card(name):
    return sim.card(out / name, files={'/a.epub': book()},
                    extra=dict(readerFavorites=PINS, readerFavoriteCount=3, readerFavoritesDaDat=1, readerTapTip=0))


def pins(sd):
    return json.loads((sd / '.crosspoint/settings.json').read_text()).get('readerFavorites')


sd = card('order')
log, shots = sim.run(a.program, out / 'order', sd, OPEN_FAVORITES + [('04-hold', HOLD_FIRST), ('05-order', REORDER),
                                                                     ('06-moved', LAST_ROW), ('07-done', DONE)], a.port)
opened = 'enter ListOrder' in log
print(f'order screen={opened} pins={pins(sd)}')
assert opened, 'RED: a held reader favourite offers no Reorder'
assert pins(sd) == ['text/fontSize', 'text/letterSpacing', 14], 'RED: the new order was not saved'
_, again = sim.run(a.program, out / 'order-restart', sd, OPEN_FAVORITES, a.port)
assert not ImageChops.difference(shots['07-done'].crop(SHEET), again['03-fav'].crop(SHEET)).getbbox(), \
    'RED: the reader Favorites lost its order after a restart (or Done did not land on Favorites)'

sd = card('cancel')
log, shots = sim.run(a.program, out / 'cancel', sd, OPEN_FAVORITES + [('04-hold', HOLD_FIRST), ('05-order', REORDER),
                                                                      ('06-moved', LAST_ROW), ('07-back', BACK)], a.port)
assert pins(sd) == PINS, f'"<" kept a new order: {pins(sd)}'
assert not ImageChops.difference(shots['03-fav'].crop(SHEET), shots['07-back'].crop(SHEET)).getbbox(), \
    '"<" did not come back to Favorites as it was'

sd = card('unpin')
sim.run(a.program, out / 'unpin', sd, OPEN_FAVORITES + [('04-hold', HOLD_FIRST), ('05-unpin', UNPIN)], a.port)
assert pins(sd) == PINS[1:], f'Unpin no longer unpins: {pins(sd)}'
print('GREEN')
