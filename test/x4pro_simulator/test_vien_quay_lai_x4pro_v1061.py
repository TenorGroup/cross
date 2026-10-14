"""X4 Pro: the way back after a jump stands in the reader strip as "< tr. N" (same checks as the X3 file).

Runs the X4 Pro simulator (pio run -e simulator_x4pro); X4PRO_PROGRAM picks another build. The jump is
the Contents tool of the reader bar (tap the foot, the second tool, the second chapter); Back is the swipe
from the left edge.
"""
import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'reading_stats_simulator'))
from test_vien_quay_lai_v1061 import REPO, WayBackPill


class X4ProWayBackPillTest(WayBackPill, unittest.TestCase):
    PROGRAM = Path(os.environ.get('X4PRO_PROGRAM', REPO / '.pio/build/simulator_x4pro/program'))
    ORIGIN = '3000:TAP:240,300;6000:TAP:240,400;8000:TAP:240,400;'
    # The ugly Home is the diary: its "continue reading" opens the book at once. The strip of a touch reader is
    # drawn in the cross grammar in both shells, so the pill is too.
    ORIGIN_UGLY = '1000:BACK;6000:TAP:240,400;8000:TAP:240,400;'
    JUMP = '10000:TAP:240,775;12000:TAP:228,754;14000:TAP:240,500;'
    BACK = '19000:SWIPE:4,400,200,400,120;'
    WHEN = {'origin': 9000, 'jumped': 16500, 'back': 21500, 'quit': 24000}


if __name__ == '__main__':
    unittest.main(verbosity=2)
