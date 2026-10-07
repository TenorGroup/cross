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


if __name__ == '__main__':
    unittest.main()
