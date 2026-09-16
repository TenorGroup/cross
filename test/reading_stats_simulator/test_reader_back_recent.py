from pathlib import Path
import sys,json,shutil,concurrent.futures,os
r=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(r/'test/reading_stats_simulator'))
import test_menu_customization as t

def check(mode):
 sd=t.o/('sd-'+mode);(sd/'nested').mkdir(parents=True,exist_ok=True);store=sd/'.crosspoint';store.mkdir(exist_ok=True)
 for name in ['old','target']:(sd/f'{name}.txt').write_text(('Một câu chuyện đang chờ bạn. Hôm nay chúng ta cùng nhau đọc một cuốn sách.\n')*80)
 shutil.copy(r/'test/epubs/test_kerning_ligature.epub',sd/'nested/book.epub')
 books=[dict(path='/old.txt',title='Sách cũ',author='A',coverBmpPath=''),dict(path='/target.txt',title='Sách cần đọc',author='B',coverBmpPath='')]
 (store/'recent.json').write_text(json.dumps(dict(books=books)))
 pins=[]
 if mode=='folder':
  prefix='1000:DOWN;1700:CONFIRM;2400:CONFIRM;';reader='EpubReader'
 elif mode=='favorites':
  path='/target.txt';h=14695981039346656037
  for b in path.encode():h=((h^b)*1099511628211)&((1<<64)-1)
  key=f'bookid/{h:016x}';d=store/'favorite-files';d.mkdir(exist_ok=True);(d/(key[7:]+'.txt')).write_text(path);pins=[key]
  prefix='1000:DOWN;1600:DOWN;2300:CONFIRM;';reader='TxtReader'
 else:
  prefix='1000:RIGHT;1800:CONFIRM;';reader='TxtReader'
 script=prefix+'6000:BACK;7600:CONFIRM;10200:BACK;12000:QUIT'
 t.run(mode,script,[(7100,'recent'),(9400,'reopened'),(11400,'final')],t.state(pins),{'backShortToFileBrowser':int(mode!='recent')})
 log=(t.o/(mode+'.log')).read_text()
 assert log.count('Entering activity: '+reader)==2,log[-4500:]
 section=log.split('Exiting activity: '+reader,1)[1]
 assert 'Entering activity: Home' in section
 assert 'Entering activity: FileBrowser' not in section,section[-3000:]
 latest=json.loads((store/'recent.json').read_text())['books'][0]['path']
 assert latest==('/nested/book.epub' if mode=='folder' else '/target.txt'),latest
 print('PASS',mode,'Back -> Recent/Continue -> same book, old preference ignored',flush=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
 for f in [pool.submit(check,mode) for mode in ['folder','favorites','recent']]:f.result()
