"""Compare the X3 Recent card's production placement and stat painter in the real framebuffer."""
from pathlib import Path
import argparse, os, subprocess
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[2];a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True)
fixture=Path(__file__).with_name('home_x3_pixels_fixture.cpp').read_text()
units=['lib/GfxRenderer/GfxRenderer.cpp','lib/GfxRenderer/FontCacheManager.cpp','lib/GfxRenderer/InlineSymbols.cpp',
       'lib/EpdFont/EpdFont.cpp','lib/EpdFont/EpdFontFamily.cpp','lib/Utf8/Utf8.cpp','lib/Epub/Epub/blocks/TextBlock.cpp',
       'lib/MiniBidi/BidiUtils.cpp','lib/MiniBidi/minibidi.c']
includes=['src','src/components','lib/EpdFont','lib/Utf8','lib/GfxRenderer','lib/Epub','lib/Memory','lib/MiniBidi',
          'lib/Serialization','lib/hal','lib/I18n','freeink-sdk/libs/display/FreeInkDisplay/include']
common=['c++','-std=c++20','-O1','-ffunction-sections','-fdata-sections','-Wl,-dead_strip','-Wno-deprecated']
common+=['-I'+str(root/'test/button_symbols/stubs')]+['-I'+str(root/i) for i in includes]
objects=[]
for index,unit in enumerate(units):
    obj=a.output/f'{index}.o'
    compile_args = ['cc','-std=c11','-O1','-c',str(root/unit),'-o',str(obj)] if unit.endswith('.c') else common+['-c',str(root/unit),'-o',str(obj)]
    subprocess.run(compile_args,check=True)
    objects.append(str(obj))
for phase in ('before','after'):
    folder=a.output/phase;folder.mkdir(exist_ok=True);inc=folder/'include/components';inc.mkdir(parents=True,exist_ok=True)
    def read(file):return subprocess.check_output(['git','show','2892ac6d:'+file],cwd=root,text=True) if phase=='before' else (root/file).read_text()
    source=read('src/activities/home/HomeActivity.cpp');(inc/'HomeExcerptStyle.h').write_text(read('src/components/HomeExcerptStyle.h'))
    methods=source[source.index('static int cardStatRunWidth('):source.index('\nvoid HomeActivity::drawRecentCard()')]
    new='HomeStatsBounds* bounds' in methods
    placement=source[source.index('  const auto size = tenorchrome::kTouchShell ? cardCoverSize(shown)'):source.index('  heldCover =', source.index('  const auto size = tenorchrome::kTouchShell ? cardCoverSize(shown)'))] if new else 'auto frame=homeCardLayout(in);'
    cpp=fixture.replace('@@METHODS@@',methods).replace('@@PLACE@@',placement).replace('@@BOUNDS@@','' if new else 'struct HomeStatsBounds { int top=0,bottom=0; };')
    if not new:cpp=cpp.replace('int drawCardStats(const HomeCardLayout& card, HomeStatsBounds* bounds=nullptr);','int drawCardStats(const HomeCardLayout& card);')
    src=folder/'paint.cpp';src.write_text(cpp);exe=folder/'paint'
    subprocess.run(common[:1]+['-I'+str(folder/'include')]+common[1:]+[str(src),*objects,'-o',str(exe)],check=True)
    subprocess.run([str(exe)],cwd=folder,check=True)
for old in sorted((a.output/'before').glob('*.pbm')):
    new=a.output/'after'/old.name
    assert old.read_bytes()==new.read_bytes(),f'X3 changed: {old.name}'
    print(f'GREEN X3 {old.name}: 0 pixels changed')
