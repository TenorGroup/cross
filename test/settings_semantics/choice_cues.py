"""Execute production cue assignments for popup, page and direct actions."""
import argparse,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2]
cases=[('StatusBarSettingsActivity.cpp',r'rowItems_\[i\]'),('ClockSettingsActivity.cpp',r'rowItems_\[i\]'),('OpdsServerListActivity.cpp','format'),('../reader/EpubReaderMenuActivity.cpp','item')]
checks=[]
for filename,item in cases:
 path=root/'src/activities/settings'/filename;s=path.read_text();expr=re.search(item+r'\.opensNext\s*=\s*(.*?);',s,re.S)[1]
 expr=expr.replace('tenorchrome::kTouchShell','touch').replace('rowChoiceStyle(i)','style').replace('rowChoiceStyle(static_cast<int>(rowItems_.size()))','style').replace('i == ITEM_TIMEZONE','true').replace('static_cast<int>(timezones::count())','count').replace('static_cast<int>(OpdsFilenameFormat::Count)','count').replace('valueCount(i)','count').replace('readermenu::rowOpens(menuItems[rowToItem[r]].action)','fallback')
 expected='style == SettingsChoiceStyle::Page' if item!='item' else '(style == SettingsChoiceStyle::None ? fallback : style == SettingsChoiceStyle::Page)'
 checks.append(f'{{bool actual=({expr});bool expected=({expected});++checks;if(actual!=expected){{++bad;printf("RED {filename} count=%d touch=%d inline=%d fallback=%d\\n",count,touch,onOff,fallback);}}}}')
tabs=(root/'src/activities/settings/SettingsTabs.cpp').read_text()
chooser=re.search(r'bool moTrinhChon\([^\n]+',tabs)[0]
cpp='#include <cstdio>\n#include "components/SettingsChoiceStyle.h"\nint main(){int bad=0,checks=0;for(bool touch:{false,true})for(int count=0;count<20;++count)for(bool onOff:{false,true})for(bool fallback:{false,true}){auto style=settingsChoiceStyle(count,touch,onOff);\n'+'\n'.join(checks)+'}\nfor(int count=0;count<20;++count){++checks;if(moTrinhChon(count)!=(count>=2)){++bad;printf("RED reader chooser count=%d\\n",count);}}\nprintf("%d caller checks, %d failures\\n",checks,bad);return bad?1:0;}'
cpp=cpp.replace('int main()', chooser+'\nint main()')
cpp=cpp.replace('#include <cstdio>','#include <cstdio>\n#include <initializer_list>')
f=a.output/'cues.cpp';f.write_text(cpp);exe=a.output/'cues';subprocess.run(['c++','-std=c++17','-I'+str(root/'src'),str(f),'-o',str(exe)],check=True);raise SystemExit(subprocess.run([str(exe)]).returncode)
