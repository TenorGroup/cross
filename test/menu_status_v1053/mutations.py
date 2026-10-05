"""Run footer, status, hints and overlay-path mutations in isolated copies."""
import argparse
from pathlib import Path
import subprocess

p=argparse.ArgumentParser()
p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2])
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.repo=a.repo.resolve();a.output=a.output.resolve()
ui=Path('src/activities/reader/ReaderToolbarUi.cpp')
activity=Path('src/activities/reader/EpubReaderActivity.cpp')
variants={
 'reserve':(ui,'screen.setContentMarginFromScreen(model_.footerInsets);',''),
 'clamp':(ui,'std::min(height, bounds.height)','height'),
 'hints':(activity,'GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);','(void)labels;'),
 'status':(activity,'tenorchrome::drawStatus(renderer);',''),
 'toolbar-call':(activity,'\n    drawMenuChrome();',''),
 'panel-call':(activity,'\n  drawMenuChrome();',''),
}
for name in ['baseline',*variants]:
 r=a.output/name;r.mkdir(parents=True,exist_ok=True)
 for file in [ui,activity]:
  dest=r/file;dest.parent.mkdir(parents=True,exist_ok=True)
  if name=='baseline':
   text=subprocess.check_output(['git','show','HEAD:'+str(file)],cwd=a.repo,text=True)
  else:
   text=(a.repo/file).read_text()
   target,old,new=variants[name]
   if file==target:
    assert old in text
    text=text.replace(old,new)
  dest.write_text(text)
 sdk=r/'freeink-sdk'
 if not sdk.exists():sdk.symlink_to(a.repo/'freeink-sdk',target_is_directory=True)
 result=subprocess.run(['python3',str(a.repo/'test/menu_status_v1053/host.py'),'--repo',str(r),'--output',str(r/'build')],capture_output=True,text=True)
 (r/'result.txt').write_text(result.stdout+result.stderr)
 assert result.returncode!=0,name+' survived'
 print('RED '+name+' exit='+str(result.returncode))
