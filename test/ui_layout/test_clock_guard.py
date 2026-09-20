from pathlib import Path
import unittest

SOURCE = Path(__file__).resolve().parents[2] / 'src/components/themes/BaseTheme.cpp'

class ClockGuard(unittest.TestCase):
    def test_status_lane_uses_valid_clock_instead_of_rtc_presence(self):
        source = SOURCE.read_text()
        body = source[source.index('void BaseTheme::drawStatusBar('):source.index('void BaseTheme::drawHelpText(')]
        self.assertNotIn('halClock.isAvailable()', body)
        self.assertIn('sb.textLaneVisible(clockstatus::hasValidTime())', body)
        self.assertIn('sb.showsClock() && clockstatus::hasValidTime()', body)

if __name__ == '__main__':
    unittest.main()
