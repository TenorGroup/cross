"""Offline regeneration: --source must be the pinned official Noto Sans SC TTF."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import sys
from font_header_tools import read_header
from check_chinese_ui import NAMES, required, fingerprint

SOURCE_SHA = 'a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da'
def main():
    from fontTools.ttLib import TTFont
    from fontTools import subset
    from fontTools.varLib.instancer import instantiateVariableFont
    p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--chinese',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--recipe',type=Path,required=True);a=p.parse_args()
    assert hashlib.sha256(a.source.read_bytes()).hexdigest()==SOURCE_SHA, 'Unexpected source font SHA256'
    sys.path.insert(0,str(a.root/'scripts'));from gen_i18n import parse_yaml_file
    cps=required(parse_yaml_file(a.chinese));recipe=json.loads(a.recipe.read_text());out=a.output;out.mkdir(parents=True,exist_ok=True)
    # Only new characters: keep every existing primary/fallback glyph unchanged.
    new=set()
    for n in NAMES:new |= cps-set(map(int,recipe[n]['hashes']))
    font=TTFont(a.source,recalcTimestamp=False);cmap=font.getBestCmap();assert not new-cmap.keys(), ('Source lacks characters',new-cmap.keys())
    opt=subset.Options();opt.layout_features=[];s=subset.Subsetter(options=opt);s.populate(unicodes=sorted(new));s.subset(font);var=out/'NotoSansSC-UI-variable.ttf';font.save(var);font.close()
    for weight in [400,500]:
        f=TTFont(var,recalcTimestamp=False);instantiateVariableFont(f,{'wght':weight},inplace=True);f.save(out/f'NotoSansSC-UI-{weight}.ttf');f.close()
    for n in NAMES:
        args=shlex.split(recipe[n]['command']);args[0]=str(a.root/'lib/EpdFont/scripts/fontconvert.py');cut=next((i for i,v in enumerate(args) if v.startswith('--')),len(args));args.insert(cut,str(out/f'NotoSansSC-UI-{500 if n.endswith("bold") else 400}.ttf'))
        for cp in sorted(new):args+=['--additional-intervals',f'{cp},{cp}']
        run=subprocess.run([sys.executable]+args,cwd=a.root/'lib/EpdFont/scripts',capture_output=True,text=True);assert run.returncode==0,run.stderr
        text=run.stdout;header=f'// Chinese UI charset SHA256: {fingerprint(cps)}\n// Noto Sans SC source SHA256: {SOURCE_SHA}\n'
        (out/(n+'.h')).write_text(header+text)
        (out/(n+'.log')).write_text(run.stderr)
    (out/'charset.json').write_text(json.dumps({'source_sha256':SOURCE_SHA,'charset_sha256':fingerprint(cps),'codepoints':sorted(cps),'added':sorted(new)},indent=2))
    print('generated',len(new),'additional characters in five UI faces')
if __name__=='__main__':main()
