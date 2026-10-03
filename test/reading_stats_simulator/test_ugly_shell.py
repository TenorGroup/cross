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

    def test_the_interface_row_switches_the_shell_and_draws_home_again(self):
        card = self.card(shell=0, sleepScreen=10)
        # Home (tenor/cross) -> Settings tab (UP) -> group Display (RIGHT) -> open (CONFIRM) -> first row: Interface
        log, _ = card.run('1000:UP;1500:RIGHT;2000:CONFIRM;2800:CONFIRM;5000:QUIT')
        names = entered(log)
        self.assertEqual(names[-1], 'UglyDiary', names)
        saved = card.settings()
        self.assertEqual(saved['uiShell'], 1)
        self.assertEqual(saved['sleepScreen'], 11, 'the quotation of tenor/cross gives way to the doodle')

    def test_the_way_back_to_tenor_cross(self):
        card = self.card(shell=1, sleepScreen=11)
        # diary -> Settings page (Left) -> Display (Down, Confirm) -> first row: Interface
        log, _ = card.run('1000:LEFT;1800:DOWN;2400:CONFIRM;3600:CONFIRM;6000:QUIT')
        self.assertEqual(entered(log)[-1], 'Home', entered(log))
        saved = card.settings()
        self.assertEqual(saved['uiShell'], 0)
        self.assertEqual(saved['sleepScreen'], 10, 'the doodle gives way to the quotation')

    def test_a_sleep_screen_chosen_by_hand_survives_a_change_of_shell(self):
        card = self.card(shell=0, sleepScreen=3)  # the cover
        card.run('1000:UP;1500:RIGHT;2000:CONFIRM;2800:CONFIRM;5000:QUIT')
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
