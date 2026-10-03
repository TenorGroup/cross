"""tenor/ugly in the simulator: the shell is chosen at run time, three tiers, one step back at a time.

Founder rules under test (04/10/2026): the interface row chooses the shell and draws Home again; tier 1
is the first screen and the one after a book; the shortcuts of tier 1 open the notebook; Back goes up
one tier; the notebook turns pages in the order of tenor/cross; the screens are deterministic; the voice of
the shell never reaches tenor/cross.

Buttons here are the ones of the card: UP and DOWN on the edge, LEFT and RIGHT in front.
"""
import re
import unittest

from ugly_common import Card, digest, entered, ink, notebook_pages

CUT_NAMES_DIGEST = '943f249c736889124a0e9dbe2b9a4941e6c0081ec03a34e7fa65b90f5546e561'
# Every width from a name that fits to one cut to a few letters, with marks, and one the baked font lacks.
LONG_NAMES = ['a.txt', 'Hành trình dài của một người.txt', 'Hành trình dài của một người đọc sách.txt',
             'Hành trình dài của một người đọc sách không bao giờ chịu đọc hết một cuốn.txt',
             'Dế Mèn phiêu lưu ký bản đầy đủ có tranh minh hoạ của nhiều hoạ sĩ khác nhau.txt',
             'WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW.txt',
             'iiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii.txt',
             '三体 Vấn đề ba vật thể phần hai Hắc ám rừng rậm.txt']
PAGE_ORDER = [0, 1, 4, 2, 3]  # Recent, Folder, Favorites, Stats, Settings: the default order of tenor/cross


class UglyShellTest(unittest.TestCase):
    def card(self, **kw):
        card = Card(**kw)
        self.addCleanup(card.close)
        return card

    def test_the_ugly_shell_opens_the_diary(self):
        log, shots = self.card().run('3000:QUIT', [(2000, 'diary')])
        self.assertEqual(entered(log), ['Boot', 'UglyDiary'], log[-1500:])
        # A page of handwriting: ink in the sentence, ink in the status bar, nothing under the sentence.
        self.assertGreater(ink(shots['diary'], (0, 80, 528, 560)), 3000)
        self.assertGreater(ink(shots['diary'], (0, 750, 528, 792)), 150)

    def test_cross_shell_keeps_home_and_none_of_the_voice(self):
        log, _ = self.card(shell=0).run('1000:RIGHT;2000:LEFT;3000:QUIT')
        self.assertIn('Entering activity: Home', log)
        self.assertNotIn('Ugly', log)
        self.assertNotIn('[UGLY]', log)

    def test_swap_books_opens_the_recent_page(self):
        log, _ = self.card().run('1000:DOWN;1600:CONFIRM;3000:QUIT')
        self.assertEqual(entered(log)[-1], 'UglyNotebook', log[-1500:])
        self.assertEqual(notebook_pages(log), [0])

    def test_the_side_shortcuts_of_the_diary(self):
        right, _ = self.card().run('1000:RIGHT;2500:QUIT')
        self.assertEqual(notebook_pages(right), [0], 'Right opens the Recent page')
        left, _ = self.card().run('1000:LEFT;2500:QUIT')
        self.assertEqual(notebook_pages(left), [3], 'Left opens the Settings page')

    def test_back_goes_up_one_tier_at_a_time(self):
        # diary -> notebook (Right) -> Back: the desk -> Back: the diary
        log, _ = self.card().run('1000:RIGHT;2000:BACK;3000:BACK;4000:QUIT')
        self.assertEqual(entered(log), ['Boot', 'UglyDiary', 'UglyNotebook', 'UglyDesk', 'UglyDiary'], log[-1500:])

    def test_the_notebook_turns_pages_in_the_order_of_tenor_cross(self):
        script = '1000:RIGHT;' + ''.join('%d:RIGHT;' % (2000 + 700 * i) for i in range(5)) + '6500:LEFT;7200:QUIT'
        log, _ = self.card().run(script)
        pages = notebook_pages(log)
        # the page it opens on, then five turns forward round the ring, then one back
        self.assertEqual(pages[:6], PAGE_ORDER + [PAGE_ORDER[0]], pages)
        self.assertEqual(pages[6], 3, 'one turn back from the first page is the last')

    def test_the_cursor_stays_on_its_row_when_a_page_is_left_and_come_back_to(self):
        # Recent, Down twice, turn to Folder and back: the circle is where it was
        log, _ = self.card().run('1000:RIGHT;1800:DOWN;2400:DOWN;3000:RIGHT;3600:LEFT;4300:QUIT')
        rows = [int(n) for n in re.findall(r'Notebook frame page=0 row=(\d+)', log)]
        self.assertEqual(rows[-1], 2, rows)

    def test_continue_reading_opens_the_book(self):
        log, _ = self.card().run('1000:CONFIRM;4000:QUIT')
        self.assertIn('Entering activity: TxtReader', log)

    def test_back_from_a_book_lands_on_the_diary(self):
        log, _ = self.card().run('1000:CONFIRM;3000:BACK;5000:QUIT')
        names = entered(log)
        self.assertEqual(names[-1], 'UglyDiary', names)

    def test_a_card_without_books_offers_a_pick_and_the_desk(self):
        log, shots = self.card(books=[]).run('1000:CONFIRM;2500:QUIT', [(800, 'empty')])
        self.assertEqual(notebook_pages(log), [1], 'Pick a book opens the Folder page')
        self.assertGreater(ink(shots['empty'], (0, 80, 528, 560)), 1500)

    def test_the_desk_is_a_picture_with_the_circle_on_the_book(self):
        log, shots = self.card().run('1000:DOWN;1600:DOWN;2200:CONFIRM;4000:QUIT', [(3500, 'desk')])
        self.assertEqual(entered(log)[-1], 'UglyDesk')
        self.assertGreater(ink(shots['desk']), 6000, 'six drawn objects, a ring and labels')
        # the measuring line says what the picture cost the heap: the plane in bytes and the largest block left
        self.assertRegex(log, r'Desk frame total=\d+ms plane=52272B heap=\d+ largest=\d+')

    def test_the_desk_walks_round_all_six_objects(self):
        script = '1000:DOWN;1600:DOWN;2200:CONFIRM;' + ''.join('%d:DOWN;' % (3000 + 600 * i) for i in range(6)) + '8000:QUIT'
        log, _ = self.card().run(script)
        self.assertEqual(log.count('Desk frame'), 7)  # the first draw and six steps, back on the first object

    def test_screens_are_deterministic(self):
        # The same screen drawn twice, in two separate runs, is the same pixels; one step away and back too.
        a = self.card().run('1000:RIGHT;2500:QUIT', [(2000, 'p')])[1]['p']
        b = self.card().run('1000:RIGHT;2500:QUIT', [(2000, 'p')])[1]['p']
        self.assertEqual(digest(a), digest(b))
        log, shots = self.card().run('1000:RIGHT;1800:DOWN;2400:UP;3600:QUIT', [(1500, 'before'), (3000, 'after')])
        self.assertEqual(digest(shots['before']), digest(shots['after']))

    def test_a_title_the_baked_font_lacks_still_draws(self):
        log, shots = self.card(books=[('三体', 'b0.txt')]).run('3000:QUIT', [(2000, 'han')])
        self.assertNotIn('not found', log)
        self.assertGreater(ink(shots['han'], (0, 80, 528, 560)), 1500)

    def test_english_has_its_own_voice(self):
        vi = self.card().run('3000:QUIT', [(2000, 'x')])[1]['x']
        en = self.card(language='EN').run('3000:QUIT', [(2000, 'x')])[1]['x']
        self.assertNotEqual(digest(vi), digest(en))

    def test_long_file_names_are_cut_where_they_always_were(self):
        # The one-pass cut must give the pixels the shave-and-measure cut gave (the digest is taken from that build).
        log, shots = self.card(books=[], files=LONG_NAMES).run('1000:RIGHT;1800:RIGHT;4000:QUIT', [(3200, 'folder')])
        self.assertEqual(notebook_pages(log)[-1], 1, log[-800:])
        self.assertEqual(digest(shots['folder']), CUT_NAMES_DIGEST)

    def folder_rows(self, log):
        return [int(n) for n in re.findall(r'Notebook frame page=1 row=\d+ rows=(\d+)', log)]

    def test_a_root_with_too_many_names_is_refused_whole_with_a_line_saying_so(self):
        # 5 books are on the card already: 1995 names fit the ceiling of 2000, 2001 do not.
        script = '1000:RIGHT;1800:RIGHT;5000:QUIT'
        fits = self.card(files=['f%04d.txt' % i for i in range(1990)]).run(script, timeout=120)[0]
        self.assertEqual(self.folder_rows(fits), [1995], fits[-800:])
        card = self.card(files=['f%04d.txt' % i for i in range(1996)])
        log, shots = card.run(script, [(4000, 'refused')], timeout=120)
        self.assertEqual(self.folder_rows(log), [0], log[-800:])
        # a notice in the place of the rows, and the screen still answers a turn of the page
        self.assertGreater(ink(shots['refused'], (0, 150, 528, 330)), 400)
        again, _ = card.run('1000:RIGHT;1800:RIGHT;2600:RIGHT;3400:LEFT;5000:QUIT', timeout=120)
        self.assertEqual(notebook_pages(again)[-3:], [1, 4, 1])

    def test_the_ceiling_follows_the_heap_that_is_left(self):
        # (30000 - 16384) / 80 = 170 names with 1 MB in one block; a heap of 30000 bytes allows 170, 18000 allows 20
        files = ['g%03d.txt' % i for i in range(100)]
        script = '1000:RIGHT;1800:RIGHT;4000:QUIT'
        roomy = self.card(files=files).run(script, CROSSPOINT_SIM_FREE_HEAP='30000')[0]
        self.assertEqual(self.folder_rows(roomy), [105], roomy[-800:])
        tight = self.card(files=files).run(script, CROSSPOINT_SIM_FREE_HEAP='18000')[0]
        self.assertEqual(self.folder_rows(tight), [0], tight[-800:])

    def test_the_interface_row_switches_the_shell_and_draws_home_again(self):
        card = self.card(shell=0, sleepScreen=10)
        # Home (tenor/cross) -> Settings tab (UP) -> group Display (RIGHT) -> open (CONFIRM) -> the row before the
        # first wraps to night mode, one more is Interface
        log, _ = card.run('1000:UP;1500:RIGHT;2000:CONFIRM;2600:LEFT;3000:LEFT;3800:CONFIRM;6000:QUIT')
        names = entered(log)
        self.assertEqual(names[-1], 'UglyDiary', names)
        saved = card.settings()
        self.assertEqual(saved['uiShell'], 1)
        self.assertEqual(saved['sleepScreen'], 11, 'the quotation of tenor/cross gives way to the doodle')

    def test_the_way_back_to_tenor_cross(self):
        card = self.card(shell=1, sleepScreen=11)
        # diary -> Settings page (Left) -> Display (Down, Confirm) -> two rows back from the first: Interface
        log, _ = card.run('1000:LEFT;1800:DOWN;2400:CONFIRM;3200:LEFT;3600:LEFT;4400:CONFIRM;7000:QUIT')
        self.assertEqual(entered(log)[-1], 'Home', entered(log))
        saved = card.settings()
        self.assertEqual(saved['uiShell'], 0)
        self.assertEqual(saved['sleepScreen'], 10, 'the doodle gives way to the quotation')

    def test_the_way_out_gives_back_the_sleep_screen_the_way_in_took(self):
        # Three screens a user can have on arrival: the tenor picture, the quotation, one picked by hand.
        for before in (8, 10, 3):
            card = self.card(shell=0, sleepScreen=before)
            card.run('1000:UP;1500:RIGHT;2000:CONFIRM;2600:LEFT;3000:LEFT;3800:CONFIRM;6000:QUIT')
            inside = card.settings()
            self.assertEqual(inside['uiShell'], 1)
            self.assertEqual(inside['sleepScreen'], 11 if before != 3 else 3)
            card.run('1000:LEFT;1800:DOWN;2400:CONFIRM;3200:LEFT;3600:LEFT;4400:CONFIRM;7000:QUIT')
            outside = card.settings()
            self.assertEqual(outside['uiShell'], 0)
            self.assertEqual(outside['sleepScreen'], before, 'sleep screen %d before the visit' % before)

    def test_a_sleep_screen_chosen_by_hand_survives_a_change_of_shell(self):
        card = self.card(shell=0, sleepScreen=3)  # the cover
        card.run('1000:UP;1500:RIGHT;2000:CONFIRM;2600:LEFT;3000:LEFT;3800:CONFIRM;6000:QUIT')
        self.assertEqual(card.settings()['sleepScreen'], 3)

    def test_the_sleep_screen_is_the_doodle_and_a_line_of_abuse(self):
        a = self.card(sleepScreen=11).run('1500:SLEEP;6000:QUIT', [(5000, 'z')])
        self.assertIn('[UGLY] sleep ready=1', a[0])
        b = self.card(sleepScreen=11).run('1500:SLEEP;6000:QUIT', [(5000, 'z')])
        self.assertEqual(digest(a[1]['z']), digest(b[1]['z']), 'the same day draws the same screen')
        self.assertGreater(ink(a[1]['z'], (0, 80, 528, 330)), 600, 'a line of handwriting above the doodle')
        self.assertGreater(ink(a[1]['z'], (100, 330, 480, 620)), 700, 'the doodle')


if __name__ == '__main__':
    unittest.main()
