"""File pins, inline button help and shared status corners through real UI input.
Run with TEST_PROGRAM and MENU_TEST_OUTPUT as in test_menu_customization.py.
"""
import json, shutil, concurrent.futures, time
from pathlib import Path
from PIL import Image, ImageChops
import test_menu_customization as t
state=t.state
saved=t.saved
t.o=t.o/('file-favorites-'+str(time.time_ns()));t.o.mkdir(parents=True)
name='Sách có tên rất dài để kiểm tra hiển thị tiếng Việt trong một thư mục với nhiều cấp và ký tự đặc biệt.epub'
folder='Thư mục sách đã dịch có tên dài'
def fixture(label):
 sd=t.o/('sd-'+label);(sd/folder).mkdir(parents=True)
 shutil.copy(t.r/'test/epubs/test_kerning_ligature.epub',sd/folder/name)
 (sd/folder/'aaa').mkdir();(sd/folder/'bbb').mkdir()
 return sd
def files(symbols=1):
 label='files-'+str(symbols);sd=fixture(label)
 script='1000:DOWN;1700:CONFIRM:900;3000:CONFIRM;3800:RIGHT;4300:RIGHT;4900:CONFIRM:900;6200:BACK:1200;7900:DOWN;8600:RIGHT;9300:CONFIRM;14500:BACK:1200;16300:QUIT'
 t.run(label,script,[(1450,'root'),(2850,'root-pin'),(3450,'folder'),(5950,'book-pin'),(8250,'favorites'),(15800,'home')],settings={'tenorButtonSymbols':symbols})
 x=saved(sd,'menu-customization.json');assert len(x['pins'])==2,x;assert any(p.startswith('folder/') for p in x['pins']);assert any(p.startswith('bookid/') for p in x['pins'])
 assert 'Entering activity: EpubReader' in (t.o/(label+'.log')).read_text()
 assert (sd/folder/name).exists()
 return sd

def deep(symbols=1):
 label='deep-'+str(symbols)
 t.run(label,'1000:DOWN;1600:DOWN;2400:QUIT',[(2100,'favorites')],state(['text/fontSize','clock/clockUtcOffsetQ','text/readerInkWeight','kosync/koServerUrl']),{'tenorButtonSymbols':symbols})
def empty(symbols=1):
 t.run('empty-'+str(symbols),'1000:DOWN;1600:DOWN;2700:QUIT',[(2200,'hint')],state(),{'tenorButtonSymbols':symbols})
def keyboard(symbols=1,axis=1):
 label=f'keyboard-{symbols}-{axis}'
 t.run(label,'1000:DOWN;1600:DOWN;2300:CONFIRM;4500:QUIT',[(4000,'help')],state(['kosync/koServerUrl']),{'tenorButtonSymbols':symbols,'keyboardAxisSwapped':axis})
def key(path,folder=False):
 h=14695981039346656037
 for c in path.encode():h=((h^c)*1099511628211)&((1<<64)-1)
 return ('folder/' if folder else 'bookid/')+f'{h^int(folder):016x}'
def record(sd,path,folder=False):
 k=key(path,folder);d=sd/'.crosspoint/favorite-files';d.mkdir(parents=True,exist_ok=True);(d/(k[7:]+'.txt')).write_text(path);return k

def status():
 sd=t.run('status','1000:DOWN;1500:DOWN;2100:CONFIRM;3300:BACK:1200;5100:QUIT',[(2900,'swap'),(4800,'home')],t.state(['status/statusBarClock']),{'statusBarClock':1})
 assert json.loads((sd/'.crosspoint/settings.json').read_text())['statusBarClock']==2
 assert 'Entering activity: StatusBarSettings' in (t.o/'status.log').read_text()
 t.run('status-restart','1000:DOWN;1500:DOWN;2400:QUIT',[(2100,'home')],reuse='status')

def reader(swap):
 name='reader-'+str(swap);sd=t.o/('sd-'+name);sd.mkdir();(sd/'chapter.txt').write_text(('Chapter example\nThis is a page of reading text.\n'*60))
 k=record(sd,'/chapter.txt')
 t.run(name,'1000:DOWN;1500:DOWN;2200:CONFIRM;4700:BACK:1200;6800:QUIT',[(4000,'reading'),(6300,'home')],t.state([k]),{'statusBarClock':swap})
 a=Image.open(t.o/(name+'-reading.png'));b=Image.open(t.o/(name+'-home.png'))
 for box in [(0,760,92,792),(435,760,528,792)]:
  assert ImageChops.difference(a.crop(box),b.crop(box)).getbbox() is None,(name,box)
 assert 'Entering activity: TxtReader' in (t.o/(name+'.log')).read_text()

def missing():
 name='missing';sd=t.o/('sd-'+name);sd.mkdir();k=record(sd,'/gone.epub')
 t.run(name,'1000:DOWN;1500:DOWN;2200:CONFIRM;3400:CONFIRM:900;4900:QUIT',[(3000,'error'),(4600,'unpin')],t.state([k]))
 assert json.loads((sd/'.crosspoint/menu-customization.json').read_text())['pins']==[]
 assert not (sd/'.crosspoint/favorite-files'/(k[7:]+'.txt')).exists()
 assert 'Entering activity: EpubReader' not in (t.o/(name+'.log')).read_text()

def folder_restart():
 name='folder-restart';sd=t.o/('sd-'+name);(sd/'one/two').mkdir(parents=True);(sd/'one/two/a.txt').write_text('Test\n')
 k=record(sd,'/one/two',True)
 t.run(name,'1000:DOWN;1500:DOWN;2200:CONFIRM;4000:QUIT',[(3500,'folder')],t.state([k]))
 assert 'Entering activity: FileBrowser' in (t.o/(name+'.log')).read_text()
 t.run('folder-remove','1000:DOWN;1500:DOWN;2200:CONFIRM:900;4000:QUIT',reuse=name)
 assert json.loads((sd/'.crosspoint/menu-customization.json').read_text())['pins']==[]
 assert (sd/'one/two/a.txt').read_text()=='Test\n'

def capacity():
 name='capacity';sd=t.o/('sd-'+name);sd.mkdir();(sd/'test.txt').write_text('Test\n')
 pins=['bookid/'+f'{i:016x}' for i in range(32)]
 t.run(name,'1000:DOWN;1600:CONFIRM:900;3400:QUIT',[(3000,'full')],t.state(pins))
 assert json.loads((sd/'.crosspoint/menu-customization.json').read_text())['pins']==pins
 assert not (sd/'.crosspoint/favorite-files').exists()

def storage():
 # Mismatched record at the exact target hash must survive a failed pin.
 sd=t.o/'sd-collision';d=sd/'.crosspoint/favorite-files';d.mkdir(parents=True);(sd/'test.txt').write_text('Book\n');k=key('/test.txt');rec=d/(k[7:]+'.txt');rec.write_text('/other.txt')
 t.run('collision','1000:DOWN;1600:CONFIRM:900;3400:QUIT',[(3000,'error')],t.state())
 assert rec.read_text()=='/other.txt';assert json.loads((sd/'.crosspoint/menu-customization.json').read_text())['pins']==[]
 # Broken menu key can be removed, without treating it as a disk path.
 sd=t.run('bad-key','1000:DOWN;1500:DOWN;2100:CONFIRM:900;3700:QUIT',state=t.state(['bookid/../bad']))
 assert json.loads((sd/'.crosspoint/menu-customization.json').read_text())['pins']==[]
 # Saving the pin store fails after writing metadata: roll back both in-memory UI and metadata.
 sd=t.o/'sd-writefail';s=sd/'.crosspoint';s.mkdir(parents=True);(s/'menu-customization.tmp').mkdir();(s/'menu-customization.tmp/block').write_text('block');(sd/'test.txt').write_text('Book\n')
 t.run('writefail','1000:DOWN;1600:CONFIRM:900;3300:DOWN;4200:QUIT',[(3900,'empty')],t.state())
 assert json.loads((s/'menu-customization.json').read_text())['pins']==[];assert not list((s/'favorite-files').glob('*.txt'));assert (sd/'test.txt').exists()

if __name__ == '__main__':
 print(t.o,flush=True)
 jobs=[('files-symbol',lambda:files(1)),('files-text',lambda:files(0)),('deep',deep),
       ('empty',empty),('empty-text',lambda:empty(0)),('keyboard',keyboard),
       ('keyboard-text',lambda:keyboard(0)),('keyboard-axis',lambda:keyboard(1,0)),
       ('corners',status),('reader-default',lambda:reader(1)),('reader-swap',lambda:reader(2)),
       ('missing',missing),('folder-restart',folder_restart),('capacity',capacity),('storage-faults',storage)]
 with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
  futures={pool.submit(f):name for name,f in jobs};failed=[]
  for f in concurrent.futures.as_completed(futures):
   try:f.result();print('PASS',futures[f],flush=True)
   except Exception as error:failed.append(futures[f]);print('FAIL',futures[f],repr(error),flush=True)
 assert not failed,failed
