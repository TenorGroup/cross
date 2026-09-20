"""A rejected morphological candidate must keep counter and detached accents."""
import sys
from pathlib import Path
import unittest
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
import build_ui_ink as ink
import numpy as np

class InkGuardTest(unittest.TestCase):
    def test_counter_fill_rejected(self):
        base=np.ones((8,8),dtype=bool);base[3,3]=False
        changed=base.copy();changed[3,3]=True
        self.assertFalse(ink.safe(base,changed))
    def test_detached_accent_merge_rejected(self):
        base=np.zeros((12,12),dtype=bool);base[5:10,2:10]=True;base[3,5]=True
        changed=base.copy();changed[4,5]=True
        self.assertFalse(ink.safe(base,changed))
    def test_counter_area_protected(self):
        base=np.ones((12,12),dtype=bool);base[4:8,4:8]=False
        changed=base.copy();changed[4:8,4:6]=True
        self.assertFalse(ink.safe(base,changed))
    def test_small_native_gain_allowed(self):
        base=np.zeros((12,12),dtype=bool);base[3:10,3:8]=True
        changed=base.copy();changed[4,8]=True
        self.assertTrue(ink.safe(base,changed))

if __name__=='__main__':unittest.main()
