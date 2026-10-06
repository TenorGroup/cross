"""tenor/ugly has no last day: a device clock past 20/10/2026 keeps it and still offers it.

Founder 06/10/2026: the limited edition is over before it began. The simulator reads the host clock, so the run is
moved 30 days on (to 5/11/2026 or later) by a small library that shifts time() for the simulator process only.
"""
import datetime
import subprocess
import tempfile
import unittest
from pathlib import Path

from ugly_common import Card, entered

SHIFT_C = r'''
#include <stdlib.h>
#include <time.h>
static time_t shifted(time_t *out) {
  time_t now = time(NULL) + (time_t)atoll(getenv("UGLY_CLOCK_SHIFT"));
  if (out) *out = now;
  return now;
}
__attribute__((used, section("__DATA,__interpose"))) static const void *pair[2] = {(const void *)shifted, (const void *)time};
'''
# Home (tenor/cross) -> Settings, open Display, the Interface row, ask; the pen to "yes" and confirm.
TO_THE_BOX = '1000:UP;2000:CONFIRM;2600:LEFT;3000:LEFT;3800:CONFIRM;'
DARE = TO_THE_BOX + '5000:LEFT;6000:CONFIRM;9000:QUIT'


class UglyNoLastDayTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='ugly-clock-')
        lib = Path(cls.temp.name) / 'shift.dylib'
        src = Path(cls.temp.name) / 'shift.c'
        src.write_text(SHIFT_C)
        subprocess.run(['cc', '-dynamiclib', '-o', str(lib), str(src)], check=True)
        later = datetime.datetime(2026, 11, 5, 3, tzinfo=datetime.timezone.utc)
        now = datetime.datetime.now(datetime.timezone.utc)
        shift = max(0, int((later - now).total_seconds()))
        cls.clock = dict(DYLD_INSERT_LIBRARIES=str(lib), UGLY_CLOCK_SHIFT=str(shift))

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def card(self, **kw):
        card = Card(**kw)
        self.addCleanup(card.close)
        return card

    def test_the_clock_is_past_the_old_last_day(self):
        # The shift reaches the simulator: the status bar clock logs the shifted day.
        probe = Path(self.temp.name) / 'probe'
        subprocess.run(['cc', '-x', 'c', '-o', str(probe), '-'], input=b'#include <stdio.h>\n#include <time.h>\n'
                       b'int main(void){char b[16];time_t t=time(0);strftime(b,16,"%Y%m%d",gmtime(&t));puts(b);}',
                       check=True)
        out = subprocess.run([str(probe)], env=self.clock, capture_output=True, text=True, check=True).stdout
        self.assertGreater(int(out), 20261020, out)

    def test_a_device_on_tenor_ugly_stays_on_it(self):
        card = self.card(shell=1, sleepScreen=11)
        log, _ = card.run('2500:QUIT', **self.clock)
        self.assertEqual(entered(log)[:2], ['Boot', 'UglyDiary'], log[-1500:])
        self.assertEqual(card.settings()['uiShell'], 1)

    def test_tenor_ugly_can_still_be_chosen(self):
        card = self.card(shell=0, sleepScreen=10)
        log, _ = card.run(DARE, **self.clock)
        self.assertIn('UglySwitch', entered(log), entered(log))
        self.assertEqual(entered(log)[-1], 'UglyDiary', entered(log))
        self.assertEqual(card.settings()['uiShell'], 1)


if __name__ == '__main__':
    unittest.main()
