"""Measure the status row with production font metrics at all UI sizes."""
import argparse,json,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[2];a.output.mkdir(parents=True,exist_ok=True)
checks=[]
for language,expected in [('vietnamese','Pin và đồng hồ'),('english','Battery and clock'),('chinese','电量与时钟')]:
    text=(root/f'lib/I18n/translations/{language}.yaml').read_text()
    label=re.search(r'^STR_STATUS_CORNERS: "(.*?)"',text,re.M)[1]
    values=[re.search(r'^'+key+r': "(.*?)"',text,re.M)[1] for key in ['STR_BATTERY_LEFT_CLOCK_RIGHT','STR_CLOCK_LEFT_BATTERY_RIGHT']]
    for tier,(body,small) in enumerate([(12,10),(14,12),(16,14)]):
        for value in values:
            checks.append(f'{{int lw=width(&geist_{body}_regular,{json.dumps(label,ensure_ascii=False)}),vw=width(&geist_{small}_regular,{json.dumps(value,ensure_ascii=False)});int gap=413-32-lw-vw;bool ok=gap>=12&&{str(label==expected).lower()};printf("%s {language} size={tier} label=%d value=%d gap=%d\\n",ok?"GREEN":"RED",lw,vw,gap);bad+=!ok;}}')
cpp='#include <EpdFont.h>\n#include <cstdio>\n'+''.join(f'#include <builtinFonts/geist_{n}_regular.h>\n' for n in (10,12,14,16))+'''int width(const EpdFontData* d,const char* s){int w,h;EpdFont(d).getTextDimensions(s,&w,&h);return w;}\nint main(){int bad=0;\n'''+ '\n'.join(checks)+'return bad?1:0;}\n'
source=a.output/'label.cpp';source.write_text(cpp);exe=a.output/'label'
subprocess.run(['c++','-std=c++17','-I'+str(root/'lib/EpdFont'),'-I'+str(root/'lib/Utf8'),str(source),str(root/'lib/EpdFont/EpdFont.cpp'),str(root/'lib/Utf8/Utf8.cpp'),'-o',str(exe)],check=True)
raise SystemExit(subprocess.run([str(exe)]).returncode)
