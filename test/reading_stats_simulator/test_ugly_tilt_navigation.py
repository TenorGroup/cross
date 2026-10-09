"""Compare injected HAL gestures with the ugly screens' physical button paths."""
import unittest

from ugly_common import Card, digest, entered


class UglyTiltNavigationTest(unittest.TestCase):
    def frame(self, prefix, button='', event='', mode=1, tabs=1):
        card = Card(tiltMenuNavigation=mode, tiltTabNavigation=tabs)
        self.addCleanup(card.close)
        script = prefix + (f'3400:{button};' if button else '') + '5600:QUIT'
        scene = {'': 'diary', '1000:DOWN;': 'recent', '1000:UP;': 'settings',
                 '1000:RIGHT;1600:RIGHT;2200:CONFIRM;': 'desk',
                 '1000:UP;1700:RIGHT;2300:CONFIRM;': 'settings-form'}[prefix]
        name = f'{scene}-rows{mode}-tabs{tabs}-{event or button or "still"}'
        log, shots = card.run(script, [(4800, name)],
                              CROSSPOINT_SIM_MENU_TILT=f'3400:{event}' if event else '')
        return log, shots[name]

    def compare(self, prefix, button, event, mode=1, tabs=1):
        _, still = self.frame(prefix, mode=mode, tabs=tabs)
        reference, moved = self.frame(prefix, button=button, mode=mode, tabs=tabs)
        tilted, actual = self.frame(prefix, event=event, mode=mode, tabs=tabs)
        self.assertNotEqual(digest(still), digest(moved), reference[-2000:])
        self.assertEqual(digest(moved), digest(actual), tilted[-2000:])

    def test_diary_and_desk_rows_use_the_same_menu_gesture(self):
        self.compare('', 'RIGHT', 'UP')
        self.compare('1000:RIGHT;1600:RIGHT;2200:CONFIRM;', 'LEFT', 'DOWN')

    def test_notebook_and_settings_rows_and_tabs(self):
        for prefix in ('1000:DOWN;', '1000:UP;'):
            self.compare(prefix, 'RIGHT', 'UP')
            self.compare(prefix, 'DOWN', 'FORWARD')

    def test_inverted_and_disabled_channels(self):
        self.compare('1000:DOWN;', 'LEFT', 'UP', mode=2)
        self.compare('1000:DOWN;', 'UP', 'FORWARD', tabs=2)
        for prefix in ('', '1000:UP;'):
            _, still = self.frame(prefix, mode=0, tabs=0)
            _, actual = self.frame(prefix, event='UP', mode=0, tabs=0)
            self.assertEqual(digest(still), digest(actual))

    def test_settings_question_form_moves_by_row_without_tab_flicks(self):
        prefix = '1000:UP;1700:RIGHT;2300:CONFIRM;'
        self.compare(prefix, 'RIGHT', 'UP')
        _, still = self.frame(prefix)
        log, actual = self.frame(prefix, event='FORWARD')
        self.assertEqual(entered(log)[-1], 'Settings')
        self.assertEqual(digest(still), digest(actual))

    def test_tab_flick_on_desk_has_no_tab_target(self):
        prefix = '1000:RIGHT;1600:RIGHT;2200:CONFIRM;'
        _, still = self.frame(prefix)
        log, actual = self.frame(prefix, event='FORWARD')
        self.assertEqual(entered(log)[-1], 'UglyDesk')
        self.assertEqual(digest(still), digest(actual))


if __name__ == '__main__':
    unittest.main()
