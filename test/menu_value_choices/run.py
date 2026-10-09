"""Execute production choice routing and cue builders with recorded side effects."""
import argparse
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--repo', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--cxx', default='c++')
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
source = (a.repo / 'src/activities/settings/TextSettingsActivity.cpp').read_text()
policy = (a.repo / 'src/activities/settings/SettingsTabs.cpp').read_text()
catalog = (a.repo / 'src/SettingsList.h').read_text()
settings = (a.repo / 'src/CrossPointSettings.h').read_text()
row_counts = []
for table in ['LAYOUT_ROW_NAME_IDS', 'STYLE_ROW_NAME_IDS']:
    labels = re.search(table + r'\[\]\s*=\s*\{(.*?)\};', source, re.S).group(1)
    for name in re.findall(r'StrId::(STR_\w+)', labels):
        entry = re.search(r'SettingInfo::(\w+)\(StrId::' + name + r'\b(.*?);', catalog, re.S)
        if entry.group(1) == 'Toggle':
            row_counts.append(2)
        elif entry.group(1) == 'Enum':
            values = re.search(r'\{(.*?)\}', entry.group(2), re.S).group(1)
            row_counts.append(len(re.findall(r'StrId::STR_\w+', values)))
        elif entry.group(1) == 'StaticEnum':
            array = re.search(r'&CrossPointSettings::\w+,\s*(\w+)\s*,', entry.group(2)).group(1)
            values = re.search(r'static constexpr StrId ' + array + r'\[\]\s*=\s*\{(.*?)\};', catalog, re.S).group(1)
            row_counts.append(len(re.findall(r'StrId::STR_\w+', values)))
        else:
            low, high, step = [int(re.search(r'SCREEN_MARGIN_' + key + r'\s*=\s*(\d+)', settings).group(1))
                               for key in ['MIN', 'MAX', 'STEP']]
            row_counts.append((high - low) // step + 1)
print('Production layout/style choice counts:', row_counts)
def method(text, name):
    start = text.index(name)
    opening = text.index('{', start)
    end, depth = opening + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

names = ['void TextSettingsActivity::confirmLayoutRow(', 'void TextSettingsActivity::confirmStyleRow(',
         'int TextSettingsActivity::formIndex(', 'void TextSettingsActivity::rebuildRowItems(',
         'const char* TextSettingsActivity::confirmLabelText(',
         'int TextSettingsActivity::settingsChoiceCount(']
if 'void TextSettingsActivity::confirmValueRow(' in source:
    names.append('void TextSettingsActivity::confirmValueRow(')
production = '\n'.join(method(source, n) for n in names)
parent = (a.repo / 'src/activities/UiListActivity.cpp').read_text()
production += '\n' + method(parent, 'void UiListActivity::showSettingsChoices(OptionPopup& popup, const StrId title,').replace(
    'UiListActivity::', 'TextSettingsActivity::')
ids = sorted(set(re.findall(r'StrId::(STR_\w+)', source)) | {'STR_SELECT', 'STR_TOGGLE', 'STR_ALIGNMENT'})
fixture = r'''
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
#include "components/SettingsChoiceStyle.h"
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#expr); failures++; } } while(0)
int failures=0;
namespace freeink::ui {struct Rect {int x=16,y=180,width=448,height=350;}; struct ListItem {const char* label=nullptr;int actionValue=0;bool opensNext=false;};}
namespace fui=freeink::ui;
namespace tenorchrome {bool kTouchShell=true;}
namespace shell {bool isUgly(){return false;}}
enum class StrId { // IDS
};
const char* tr(StrId id){return id==StrId::STR_SELECT?"Select":"Toggle";}
constexpr StrId STR_SELECT=StrId::STR_SELECT,STR_TOGGLE=StrId::STR_TOGGLE;
struct {const char* get(StrId){return "label";}} I18N;
constexpr int ACTION_ROW=1;
[[maybe_unused]] constexpr int MARGIN_MIN=0,MARGIN_MAX=20,MARGIN_STEP=1;
constexpr StrId LAYOUT_ROW_NAME_IDS[7]={},STYLE_ROW_NAME_IDS[5]={};
[[maybe_unused]] constexpr StrId ALIGNMENT_IDS[5]={};
namespace settingstabs { bool moTrinhChon(int soLuaChon) // POLICY
}
struct SettingInfo {StrId nameId=StrId::STR_ALIGNMENT;};
struct Popup {
 int opened=0,current=-1;bool aligned=false;fui::Rect frame{};std::vector<std::string> labels;std::function<void(int)> callback;
 void show(StrId,const std::vector<std::string>& opts,int selected,std::function<void(int)> cb){opened++;labels=opts;current=selected;callback=cb;}
 void show(StrId,const StrId*,int count,int selected,std::function<void(int)> cb){opened++;labels.assign(count,"label");current=selected;callback=cb;}
 void alignValueTo(fui::Rect rect){frame=rect;aligned=true;}
};
using OptionPopup=Popup;
struct SettingsChoiceActivity {
 std::vector<std::string> labels;int current;std::function<void(int)> callback;
 SettingsChoiceActivity(int,int,const char*,const std::vector<std::string>& opts,int selected,std::function<void(int)> cb)
   :labels(opts),current(selected),callback(cb){}
};
struct TextSettingsActivity {
 enum class Tab{Family,Size,Layout,Style,Count};
 enum class LayoutRow{LineSpacing,LetterSpacing,WordSpacing,ParaSpacing,Alignment,ScreenMargin,ParaIndent,Count};
 enum class StyleRow{FocusReading,Hyphenation,EmbeddedStyle,AntiAliasing,InkWeight,Count};
 struct Row {int count=0,selected=0;};
 struct Name{std::string name="name";};
 Tab tab_=Tab::Layout;Popup optionPopup_;std::vector<fui::ListItem> rowItems_;std::vector<std::string> rowValues_;std::vector<Name> fonts_,sizes_;
 int applied=-1,saves=0,updates=0,selected=3,top=1,ring=1;
 int renderer=0,mappedInput=0;
 struct {fui::Rect publishedRect(int,int16_t){return {16,180,448,350};}} app;
 std::unique_ptr<SettingsChoiceActivity> page;
 void startActivityForResult(std::unique_ptr<SettingsChoiceActivity> next,std::nullptr_t){page=std::move(next);}
 static int formIndex(Tab,int);
 static Row formRow(void* context,int question){auto* self=static_cast<TextSettingsActivity*>(context);if(question<2||question>=14)return {};const int counts[]={// COUNTS
 };int n=counts[question-2];return {n,std::min(self->selected,n-1)};}
 static const SettingInfo* formSetting(int question){static SettingInfo info;return question>=2&&question<14?&info:nullptr;}
 static void formLabel(void*,int,int option,char* buf,size_t n){std::snprintf(buf,n,"value-%d",option);}
 void applyChosenValue(Tab,int,int option){applied=option;saves++;}
 int listCount() const{return tab_==Tab::Layout?7:tab_==Tab::Style?5:0;}
 bool rowFrameFor(int,int,fui::Rect& r){r={16,180,448,350};return true;}
 int ringPos() const{return ring;}
 void requestUpdate(){updates++;}
 int settingsChoiceCount(int) const;
 void showSettingsChoices(OptionPopup&,StrId,const std::vector<std::string>&,int,int,std::function<void(int)>);
 void confirmLayoutRow(int);void confirmStyleRow(int);void confirmValueRow(Tab,int);void rebuildRowItems();const char* confirmLabelText() const;
};
// PRODUCTION
int main(){
 CHECK(!settingstabs::moTrinhChon(0));CHECK(!settingstabs::moTrinhChon(1));CHECK(!settingstabs::moTrinhChon(2));CHECK(settingstabs::moTrinhChon(3));CHECK(settingstabs::moTrinhChon(4));
 int checked=0;
 for(bool touch:{false,true})for(auto tab:{TextSettingsActivity::Tab::Layout,TextSettingsActivity::Tab::Style}){
  tenorchrome::kTouchShell=touch;TextSettingsActivity screen;screen.tab_=tab;screen.rebuildRowItems();
  for(int row=0;row<screen.listCount();row++){
   auto current=TextSettingsActivity::formRow(&screen,TextSettingsActivity::formIndex(tab,row));bool picker=current.count>2;
   bool next=touch?current.count>=7:picker;
   CHECK(screen.rowItems_[row].opensNext==next);screen.ring=row+1;
   CHECK(std::string(screen.confirmLabelText())==(next?"Select":"Toggle"));
   for(int initial:{0,current.count-1}){
    TextSettingsActivity s;s.tab_=tab;s.selected=initial;int top=s.top;
    if(tab==TextSettingsActivity::Tab::Layout)s.confirmLayoutRow(row);else s.confirmStyleRow(row);
    CHECK(s.selected==initial && s.top==top);
    if(picker){
     bool page=touch&&current.count>=7;
     CHECK(s.optionPopup_.opened==int(!page) && bool(s.page)==page && s.applied==-1 && s.saves==0);
     int selected=s.page?s.page->current:s.optionPopup_.current;
     const auto& labels=s.page?s.page->labels:s.optionPopup_.labels;
     const auto& callback=s.page?s.page->callback:s.optionPopup_.callback;
     CHECK(selected==initial && int(labels.size())==current.count);
     CHECK(s.optionPopup_.aligned==(touch&&!page));
     if(touch&&!page)CHECK(s.optionPopup_.frame.x==16&&s.optionPopup_.frame.y==180&&s.optionPopup_.frame.width==448&&s.optionPopup_.frame.height==350);
     CHECK(bool(callback));
     if(callback)for(int option=0;option<current.count;option++){callback(option);CHECK(s.applied==option);}
    } else CHECK(s.optionPopup_.opened==0 && s.applied==(initial+1)%current.count && s.saves==1);
    checked++;
   }
  }
 }
 for(bool touch:{false,true})for(int count:{0,1,2,3,6,7,8}){
  tenorchrome::kTouchShell=touch;
  CHECK(settingsChoiceStyle(count,touch)==(count<=2?SettingsChoiceStyle::Inline:
        touch&&count>=7?SettingsChoiceStyle::Page:SettingsChoiceStyle::Popup));
  if(count<=2)continue;
  TextSettingsActivity s;std::vector<std::string> labels(count,"choice");
  s.showSettingsChoices(s.optionPopup_,StrId::STR_ALIGNMENT,labels,count-1,0,[&s](int value){s.applyChosenValue(s.tab_,0,value);});
  bool page=touch&&count>=7;
  CHECK(bool(s.page)==page&&s.optionPopup_.opened==int(!page)&&s.updates==1);
  CHECK(s.optionPopup_.aligned==(touch&&!page));
  CHECK((s.page?s.page->current:s.optionPopup_.current)==count-1);
  CHECK((s.page?s.page->labels:s.optionPopup_.labels)==labels);
  const auto& callback=s.page?s.page->callback:s.optionPopup_.callback;
  CHECK(bool(callback));if(callback){callback(0);CHECK(s.applied==0&&s.saves==1);}
 }
 TextSettingsActivity invalid;invalid.confirmLayoutRow(-1);invalid.confirmLayoutRow(7);invalid.confirmStyleRow(-1);invalid.confirmStyleRow(5);CHECK(invalid.optionPopup_.opened==0 && invalid.saves==0);
 std::printf("%d activation paths, 12 row cues per profile, all option callbacks: %s\n",checked,failures?"FAIL":"PASS");return failures?1:0;
}
'''
fixture = fixture.replace('// IDS', ','.join(ids)).replace('// POLICY', method(policy, 'bool moTrinhChon(')[method(policy,'bool moTrinhChon(').index('{'):]).replace('// PRODUCTION', production)
fixture = fixture.replace('// COUNTS', ','.join(map(str, row_counts)))
src = a.output / 'choices.cpp'
exe = a.output / 'choices'
src.write_text(fixture)
subprocess.run([a.cxx, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I' + str(a.repo / 'src'), str(src), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
