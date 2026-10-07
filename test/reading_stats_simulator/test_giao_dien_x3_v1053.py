"""v1.0.53: lists on the button boards (X3, X4), founder 07/10/2026 after trying v1.0.53 on the X3.

Paging: in every list, the front Down on the last row of a page opens the next page with its first row
chosen, the front Up on the first row opens the page before with its last row chosen. The Settings and File
lists have paged since v1.0.52 (test_duyet_theo_trang_v1052); the toolbar reader menu and the value list it
opens over its sheet crawled a row a press. The last page reaches the foot, as on every list of the buttons.
"""
import unittest

from test_menu_thanh_x3_v1053 import ROW, TO_TEXT_ROW, cursor_top, keep, parallel, same, sheet_top


def cursor_rows(shots, first):
    """The cursor's y on each shot from `first` on, under the sheet's title."""
    top = sheet_top(shots[first]) + 60
    return [cursor_top(img, top) for img in shots[first:]]


class PagingTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # The Text sheet (14 rows): 13 steps down to its last row, then one up.
        text = TO_TEXT_ROW(0) + ['RIGHT'] * 13 + ['LEFT']
        # Down to the first row of page 2 and back up.
        back = TO_TEXT_ROW(0) + ['RIGHT'] * 5 + ['LEFT']
        # The margin values (8, from 5 to 40) over the sheet, opened on the value in use (5): to the last, one up.
        margin = TO_TEXT_ROW(ROW['screenMargin']) + ['CONFIRM'] + ['RIGHT'] * 7 + ['LEFT']
        cls.text, cls.back, cls.margin = parallel([text, back, margin])
        for name, res in (('text', cls.text), ('margin', cls.margin)):
            for i, img in enumerate(res['shots']):
                keep(f'trang-{name}-{i}', img)

    def page_turn(self, shots, first):
        """The cursor's y per step and the rows of the first page; the step past them must turn the page."""
        ys = cursor_rows(shots, first)
        self.assertTrue(all(y is not None for y in ys), ys)
        rows = next(k for k in range(1, len(ys)) if ys[k] <= ys[k - 1])
        self.assertGreater(rows, 1, ys)
        # A crawl leaves the cursor on the bottom row; a page turn takes it up with the rows.
        self.assertLess(ys[rows], ys[rows - 1], f'step {rows} crawled one row instead of turning the page: {ys}')
        return rows, ys

    def test_text_sheet_turns_whole_pages(self):
        rows, ys = self.page_turn(self.text['shots'], 2)  # shot 2: the Text sheet, cursor on its first row
        self.assertEqual(ys[rows], ys[0], f'the next page opens on its first row: {ys}')
        # The last row, then Up: one row back on the same page.
        self.assertLess(ys[-1], ys[-2], ys)

    def test_up_from_a_first_row_is_the_page_before(self):
        s = self.back['shots']
        rows, ys = self.page_turn(self.text['shots'], 2)
        self.assertEqual(rows, 5, ys)
        self.assertTrue(same(s[-1], s[-3]), 'Up from the first row of page 2 shows page 1 as it was left')

    def test_value_list_turns_whole_pages(self):
        first = 2 + ROW['screenMargin'] + 1  # the values open over the sheet
        _, ys = self.page_turn(self.margin['shots'], first)
        self.assertLess(ys[-1], ys[-2], ys)


PITCH = 56  # the row pitch of the X3 lists and of the toolbar sheet


def value_right(img, y, x0=280, x1=512):
    """Right end of the words at the right of a row from `y` (the value): the grey ">" is left out, its half-ink
    checker has no 2 dark pixels side by side. The side arrows past x1 are left out."""
    px = img.load()
    rows = range(y + 8, y + PITCH - 8)
    cols = [x for x in range(x0, x1) if any(px[x, yy] < 128 for yy in rows)]
    clusters = []
    for x in cols:
        if clusters and x - clusters[-1][-1] < 6:
            clusters[-1].append(x)
        else:
            clusters.append([x])
    solid = [c for c in clusters
             if any(px[x, yy] < 128 and px[x + 1, yy] < 128 for x in c[:-1] for yy in rows)]
    return solid[-1][-1] if solid else None


def settings_shot(keys):
    """Home on the tenor shell, keys a press every 900 ms, one frame after the last."""
    from ugly_common import Card
    card = Card(shell=0, books=[])
    try:
        t = 1500
        script = []
        for k in keys:
            script.append(f'{t}:{k}')
            t += 900
        script.append(f'{t + 1500}:QUIT')
        _, shots = card.run(';'.join(script), [(t + 900, 'frame')], timeout=90)
    finally:
        card.close()
    return shots['frame'].convert('L')


class ValueColumnTest(unittest.TestCase):
    """A value on a row with no ">" ends on the column of the values before a ">" (founder 07/10/2026)."""

    def test_display_settings(self):
        # Settings > Display, cursor on its first row (y 129): rows 1..9, "15 trang >" among "Mặc định", "Bật"...
        img = settings_shot(['DOWN'] * 4 + ['CONFIRM'])
        keep('cot-hien-thi', img)
        rights = [value_right(img, 129 + PITCH * k) for k in range(1, 10)]
        self.assertTrue(all(rights), rights)
        self.assertLessEqual(max(rights) - min(rights), 1, f'values end on {rights}')

    def test_toolbar_text_sheet(self):
        # Page 2 of the Text sheet, cursor on its first row: 3 rows with ">" and "Định dạng gốc của sách: Bật".
        res = parallel([TO_TEXT_ROW(5)])[0]
        img = res['shots'][-1]
        keep('cot-van-ban', img)
        top = cursor_top(img, sheet_top(img) + 60)
        rights = [value_right(img, top + PITCH * k, x1=505) for k in range(1, 5)]
        self.assertTrue(all(rights), rights)
        self.assertLessEqual(max(rights) - min(rights), 1, f'values end on {rights}')


if __name__ == '__main__':
    unittest.main()
