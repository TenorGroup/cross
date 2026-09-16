"""Shared tab and hint geometry through real X3/X4 simulator input.
Use TEST_PROGRAM and MENU_TEST_OUTPUT as in test_menu_customization.py.
"""
import sys,shutil,os,concurrent.futures
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_menu_customization as t
from PIL import Image,ImageChops

def reader():
 name='reader-layout';sd=t.o/('sd-'+name);sd.mkdir(parents=True,exist_ok=True);shutil.copy(t.r/'test/epubs/test_kerning_ligature.epub',sd/'book.epub')
 t.run(name,'1000:DOWN;1700:CONFIRM;8000:CONFIRM;9100:DOWN;9900:DOWN;10800:CONFIRM;12700:DOWN;14500:BACK;16200:QUIT',[(1400,'home'),(8700,'favorites'),(9500,'position'),(10400,'reading'),(12100,'text'),(14000,'text-tab')],settings={'readerFavorites':[16,17,13],'readerFavoriteCount':3,'readerFavoritesDaDat':1})
 log=(t.o/(name+'.log')).read_text();assert log.count('Entering activity: TextSettings')==1,log[-3000:]
 # Shared tab underline geometry, each band occupies exactly the same rows.
 for label in ['home','favorites','position','reading','text','text-tab']:
  im=Image.open(t.o/(name+'-'+label+'.png')).convert('L')
  dark=[]
  for y in range(50,120):
   n=sum(im.getpixel((x,y))<100 for x in range(20,im.width-20))
   if n>=30:dark.append(y)
  assert max(dark)==107,(label,min(dark),max(dark))
  assert min(dark)>=53,(label,min(dark))
 print('PASS reader font-menu removal, old pins, shared tab bottom',flush=True)
def hints():
 name='hints';sd=t.o/('sd-'+name);(sd/'books').mkdir(parents=True,exist_ok=True);(sd/'books/a').mkdir(exist_ok=True)
 t.run(name,'1000:DOWN;2000:CONFIRM;3300:BACK:1200;5000:UP;5800:RIGHT;6600:CONFIRM;8000:QUIT',[(1500,'folder'),(2900,'nested'),(7500,'settings')])
 a=Image.open(t.o/(name+'-folder.png'));b=Image.open(t.o/(name+'-nested.png'));c=Image.open(t.o/(name+'-settings.png'))
 for v in [b,c]:assert ImageChops.difference(a.crop((100,726,425,748)),v.crop((100,726,425,748))).getbbox() is None
 print('PASS exact pin-tip font and position across Folder, nested folder, Settings',flush=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as p:
 for f in [p.submit(reader),p.submit(hints)]:f.result()
