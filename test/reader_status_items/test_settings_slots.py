"""Run production settings row binding, choice navigation and value application."""
from pathlib import Path
import argparse
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
p.add_argument('--build', type=Path, required=True)
p.add_argument('--mutation', action='store_true')
a = p.parse_args()
repo = a.repo

def method(text, signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

source = (repo / 'src/activities/settings/StatusBarSettingsActivity.cpp').read_text()
constants = source[source.index('constexpr StrId TENOR_NAMES'):source.index('}  // namespace')]
constants = constants.replace('static_assert(TENOR_ROW_COUNT == StatusBarSettingsActivity::MAX_STATUS_BAR_ITEMS,\n              "keep StatusBarSettingsActivity::MAX_STATUS_BAR_ITEMS in sync with TENOR_ROWS");', '')
methods = '\n'.join(method(source, signature) for signature in [
    'void StatusBarSettingsActivity::handleSelection(', 'int StatusBarSettingsActivity::listCount(',
    'void StatusBarSettingsActivity::closeChoices(', 'bool StatusBarSettingsActivity::applyChosenValue(',
    'ugly::QuestionSheet::Row StatusBarSettingsActivity::formRow(',
    'void StatusBarSettingsActivity::formLabel('])
if a.mutation:
    methods = methods.replace('choiceRow_ = -1;', 'choiceRow_ = 1;')

cpp = r'''
#include <array>
#include <cassert>
#include <cstdio>
#include <string>
#include "CrossPointSettings.h"
#include "I18n.h"
namespace ugly {struct QuestionSheet {
 enum class Kind {Toggle,Choice};
 struct Row {uint32_t id=0;const char* question=nullptr;Kind kind=Kind::Toggle;int count=0,selected=0;};
};}
struct Nav {int selected=0,top=0;void reset(){selected=top=0;}};
struct Rect {int y,height;};
struct App {Rect publishedRect(int,int index){return {80+index*70,62};}};
struct RenderLock {template<class T> RenderLock(T&) {}};
class StatusBarSettingsActivity {
 public:
 int visibleItemCount=4,choiceRow_=-1,choiceTop_=0,choiceBottom_=0,choiceRowHeight_=0,rowFrameGap=8;
 int resets=0,updates=0,saves=0;Nav nav,choiceNav_;App app;static constexpr int ACTION_ROW=1;
 struct Lines {int top,bottom;};
 Lines rowFrameLines(int){return {4,4};}
 void resetUi(){++resets;}void requestUpdate(){++updates;}
 bool saveSettings(bool){++saves;return true;}
 void handleSelection();int listCount()const;void closeChoices();
 bool applyChosenValue(int,int,bool=true);
 static ugly::QuestionSheet::Row formRow(void*,int);
 static void formLabel(void*,int,int,char*,size_t);
};
''' + constants + methods + r'''
int main(){
 using S=CrossPointSettings;
 StatusBarSettingsActivity view;
 SETTINGS.readerStatusBarMode=S::READER_STATUS_BAR_BOOK_DETAILS;
 SETTINGS.adoptReaderStatusItems();
 for(int row=0;row<4;++row){
   view.nav.selected=row;view.handleSelection();
   assert(view.choiceRow_==row&&view.listCount()==(row?7:3));
   assert(view.choiceTop_==76&&view.choiceBottom_==356&&view.choiceRowHeight_==62);
   assert(view.choiceNav_.selected==SETTINGS.readerStatusItem(row));
   const auto binding=view.formRow(&view,row);
   assert(binding.kind==ugly::QuestionSheet::Kind::Choice&&binding.count==(row?7:3));
   for(int value=0;value<binding.count;++value){
     char label[128];view.formLabel(&view,row,value,label,sizeof(label));assert(label[0]);
     assert(view.applyChosenValue(row,value));
     assert(SETTINGS.readerStatusItem(row)==value);
   }
   assert(!view.applyChosenValue(row,255));
   view.closeChoices();assert(view.choiceRow_==-1&&view.listCount()==4&&view.nav.selected==row);
 }
 assert(view.resets==8&&view.updates==4);
 puts("settings_slots:GREEN (4 bindings, 24 choices, same parent frame, back restores parent cursor)");
}
'''
with tempfile.TemporaryDirectory() as folder:
    path = Path(folder) / 'flow.cpp'
    out = Path(folder) / 'flow'
    path.write_text(cpp)
    flags = (a.build / 'CMakeFiles/ReaderStatusSlotsTest.dir/flags.make').read_text()
    import shlex
    compiler_flags = []
    for key in ['CXX_DEFINES', 'CXX_INCLUDES', 'CXX_FLAGS']:
        line = next(line for line in flags.splitlines() if line.startswith(key + ' = '))
        compiler_flags += shlex.split(line.split(' = ', 1)[1])
    objects = [str(x) for x in (a.build / 'CMakeFiles/ReaderStatusSlotsTest.dir').rglob('*.o')
               if x.name != 'ReaderStatusSlotsTest.cpp.o']
    subprocess.run(['c++'] + compiler_flags + ['-UNDEBUG', str(path)] + objects + ['-Wl,-dead_strip', '-o', str(out)], check=True)
    subprocess.run([str(out)], check=True)
