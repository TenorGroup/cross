import contextlib, io, sys, unittest
from pathlib import Path
REPO=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(REPO/'lib/EpdFont/scripts'))
import fontconvert_sdcard as converter
FONT=REPO/'lib/EpdFont/builtinFonts/source/NotoSans/NotoSans-Regular.ttf'
class OutlineTest(unittest.TestCase):
 def raster(self,pt,weight=0):
  with contextlib.redirect_stderr(io.StringIO()):
   return converter.rasterize_font_style(str(FONT),pt,[(32,126),(0x1ea0,0x1ef9)],embolden_px=weight)
 def test_levels_keep_advances_and_greys(self):
  for pt in (12,14,16,18,20,22,24,26):
   data=[self.raster(pt,w) for w in (0,.2,.35)]
   # All records preserve advance and retain actual four-level bitmaps.
   ref=[(g.code_point,g.advance_x) for g,b in data[0].all_glyphs]
   totals=[]
   for d in data:
    self.assertEqual(ref,[(g.code_point,g.advance_x) for g,b in d.all_glyphs])
    values=[(b[i//4]>>(6-2*(i%4)))&3 for g,b in d.all_glyphs for i in range(g.width*g.height)]
    self.assertEqual(set(values),{0,1,2,3})
    totals.append(sum(values))
   self.assertLess(totals[0],totals[1]); self.assertLess(totals[1],totals[2])
 def test_invalid_strength_rejected(self):
  for strength in (-1,float('nan'),float('inf'),2):
   with self.assertRaises(ValueError): self.raster(14,strength)
if __name__=='__main__': unittest.main()
