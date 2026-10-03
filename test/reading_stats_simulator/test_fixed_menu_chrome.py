"""Shared tab and hint geometry through real X3/X4 simulator input.
Use TEST_PROGRAM and MENU_TEST_OUTPUT as in test_menu_customization.py.
"""
import sys,shutil,os,concurrent.futures,json
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_menu_customization as t
from PIL import Image,ImageChops

def centered_ink_bands(image,top,bottom):
 im=image.convert('L')
 if im.width>im.height:im=im.rotate(90,expand=True)
 bands=[];start=None
 for y in range(top,bottom):
  ink=any(im.getpixel((x,y))<128 for x in range(90,438))
  if ink and start is None:start=y
  elif not ink and start is not None:bands.append((start,y));start=None
 if start is not None:bands.append((start,bottom))
 return bands

def reader():
 name='reader-layout';sd=t.o/('sd-'+name);sd.mkdir(parents=True,exist_ok=True);shutil.copy(t.r/'test/epubs/test_kerning_ligature.epub',sd/'book.epub')
 t.run(name,'1000:DOWN;1700:CONFIRM;8000:CONFIRM;9100:DOWN;9900:DOWN;10800:CONFIRM;12700:DOWN;14500:BACK;16200:QUIT',[(1400,'home'),(8700,'favorites'),(9500,'position'),(10400,'reading'),(12100,'text'),(14000,'text-tab')],settings={'readerFavorites':[16,17,13],'readerFavoriteCount':3,'readerFavoritesDaDat':1})
 log=(t.o/(name+'.log')).read_text();assert log.count('Entering activity: TextSettings')==1,log[-3000:]
 # Shared tab bar geometry, each band occupies exactly the same rows.
 for label in ['home','favorites','position','reading','text','text-tab']:
  im=Image.open(t.o/(name+'-'+label+'.png')).convert('L')
  dark=[]
  for y in range(50,120):
   n=sum(im.getpixel((x,y))<100 for x in range(20,im.width-20))
   if n>=30:dark.append(y)
  assert max(dark)==112,(label,min(dark),max(dark))  # the round bar, rows 53..112
  assert min(dark)>=53,(label,min(dark))
 print('PASS reader font-menu removal, old pins, shared tab bottom',flush=True)
def hints():
 name='hints';sd=t.o/('sd-'+name);(sd/'books').mkdir(parents=True,exist_ok=True);(sd/'books/a').mkdir(exist_ok=True)
 t.run(name,'1000:DOWN;2000:CONFIRM;3300:BACK:1200;5000:UP;5800:RIGHT;6600:CONFIRM;8000:QUIT',[(1500,'folder'),(2900,'nested'),(7500,'settings')])
 a=Image.open(t.o/(name+'-folder.png'));b=Image.open(t.o/(name+'-nested.png'));c=Image.open(t.o/(name+'-settings.png'))
 for v in [b,c]:assert ImageChops.difference(a.crop((100,726,425,748)),v.crop((100,726,425,748))).getbbox() is None
 measured=[]
 # The Vietnamese pin hint contains a real g descender. Small symbolic footers
 # leave a blank gap before the logical symbol lane; large footers retain a
 # caption band at every UI tier. Back and Select are now fixed
 # 1-bit bitmaps (InlineSymbolBitmaps.h) instead of a tier-scaled procedural
 # glyph, so both are a touch taller at the smallest tier than the shapes they
 # replaced: the small gap is 1px (not 2px) at tier 0, and the large band
 # grows by up to 2px. Re-measured 23/09/2026 against the new glyphs.
 # 03/10/2026: the tier 0 and tier 1 captions are Geist 8 and Geist 10, one pixel taller than the fonts
 # before them (22 against 21, 27 against 26), so their large bands sit one pixel higher:
 # (728,743) where it was (729,744), and (685,701) where it was (686,702).
 growth=(0,5,12);large_bands=((728,743),(685,701),(670,690));small_gaps=(1,2,2)
 for tier in range(3):
  row={'tier':tier}
  for mode in (0,2):
   label=f'footer-{tier}-{mode}';root=t.o/('sd-'+label);(root/'books/a').mkdir(parents=True)
   t.run(label,'1000:DOWN;2400:QUIT',[(1800,'folder')],settings={'uiTextSize':tier,'globalStatusBarMode':mode,'tenorButtonSymbols':1})
   image=Image.open(t.o/(label+'-folder.png'))
   if mode==0:
    symbol_lane_top=772-growth[tier]
    bands=centered_ink_bands(image,symbol_lane_top-40,symbol_lane_top)
    assert len(bands)==1,(tier,mode,bands)
    gap=small_gaps[tier]
    assert symbol_lane_top-bands[0][1]==gap,(tier,mode,symbol_lane_top,bands)
    row['small_tip_band']=bands[0];row['small_symbol_lane_top']=symbol_lane_top;row['small_gap']=gap
   else:
    bands=centered_ink_bands(image,640,750)
    assert bands==[large_bands[tier]],(tier,mode,bands,large_bands[tier])
    row['large_tip_band']=bands[0]
  measured.append(row)
 (t.o/'footer-geometry.json').write_text(json.dumps(measured,indent=2)+'\n')
 print('PASS exact pin-tip position, small descender gap, and unchanged large anchor across three tiers',flush=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as p:
 for f in [p.submit(reader),p.submit(hints)]:f.result()
