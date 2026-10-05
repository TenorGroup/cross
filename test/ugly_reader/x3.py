"""Execute non-X4 reader sheet/choice geometry with both paint styles and the real SDK."""
from pathlib import Path
import argparse
import re
import subprocess
p=argparse.ArgumentParser();p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2]);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
subprocess.run(['python3',str(a.repo/'test/ugly_reader/skin.py'),'--repo',str(a.repo),'--output',str(a.output)],check=True)
source=(a.repo/'src/activities/reader/ReaderToolbarUi.cpp').read_text()
def method(text,signature):
 start=text.index(signature);brace=text.index('{',start);end=brace+1;depth=1
 while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]
cpp=(a.output/'layout.cpp').read_text().replace('#define FREEINK_DEVICE_X4PRO 1','#define FREEINK_DEVICE_X4PRO 0')
for name in ['buildX4Tools','buildX4Toolbar','buildX4Panel','buildX4Spacing','buildX4Keypad','scrollRows']:
 match=re.search(r'^[^\n]*ReaderToolbarUi::'+name+r'\(',cpp,re.M)
 if match:cpp=cpp.replace(method(cpp,match.group(0)),'')
cpp=cpp.replace(method(cpp,'fui::Rect readerFrame'),'')
for name in ['buildToolbar','buildPanel']:
 cpp=cpp.replace(method(cpp,'void ReaderToolbarUi::'+name),method(source,'void ReaderToolbarUi::'+name))
extra='\n'.join(re.findall(r'^constexpr (?:int|int16_t) .*?;',source,re.M))+'\n'
for name in ['buildSheet','buildToolRow','drawChoices','drawMarkedRows']:extra+=method(source,'void ReaderToolbarUi::'+name)+'\n'
cpp=cpp.replace('void ReaderToolbarUi::buildToolbar',extra+'void ReaderToolbarUi::buildToolbar',1)
cpp=cpp.replace('freeink::Icon icon_reader_back_24,icon_reader_next_24,icon_reader_tick_24;','freeink::Icon icon_reader_back_24,icon_reader_next_24,icon_reader_tick_24,icon_reader_contents_24,icon_reader_text_24,icon_reader_more_24;')
cpp=cpp.replace('namespace fui = freeink::ui;','namespace fui = freeink::ui;\nint screenW=528,screenH=792;\nstruct UITheme { struct Metrics { int listRowHeight=48; }; static UITheme getInstance(){return {};} Metrics getMetrics()const{return {};} };',1).replace('d.width=480; d.height=800;','d.width=screenW; d.height=screenH;')
cpp=cpp[:cpp.index('int main()')]+r'''
int main(){
 GfxRenderer renderer;int scenarios=0;
 for(int tier:{0,1,2})for(auto size:{fui::Size{528,792},fui::Size{792,528}})for(bool dense:{false,true}){
  screenW=size.width;screenH=size.height;renderer.clip={0,0,screenW,screenH};
  ReaderToolbarUi ui(renderer);ui.uiTarget.tier=tier;ui.begin();
  ReaderToolbarUi::Model model;model.panel=true;model.itemCount=31;model.selectedIndex=0;model.panelTitle="Font family";
  model.footerInsets={0,0,44,0};model.denseRows=dense;
  model.rowText=[](int i){return std::string(i%2?"Paragraph alignment":"Font family");};
  model.rowValue=[](int){return std::string("Default");};
  model.choiceCount=[](int i){return i%2?3:0;};model.choiceInUse=[](int){return 1;};
  model.choiceIcon=[](int,int){return &icon_reader_tick_24;};
  model.rowMarked=[](int i){return i%2==0;};
  shell::enabled=false;ui.setModel(model);ui.render();
  std::vector<fui::Rect> rects;
  for(int i=0;i<31;++i)rects.push_back(ui.app.publishedRect(ACTION_ROW,i));
  for(int i=0;i<31*8;++i)rects.push_back(ui.app.publishedRect(ACTION_CHOICE,i));
  int before=renderer.pen;shell::enabled=true;ui.render();assert(renderer.pen>before);
  size_t j=0;
  for(int i=0;i<31;++i){auto r=ui.app.publishedRect(ACTION_ROW,i),c=rects[j++];assert(r.x==c.x&&r.y==c.y&&r.width==c.width&&r.height==c.height);}
  for(int i=0;i<31*8;++i){auto r=ui.app.publishedRect(ACTION_CHOICE,i),c=rects[j++];assert(r.x==c.x&&r.y==c.y&&r.width==c.width&&r.height==c.height);}
  assert(!ui.app.interactionOverflowed());
  assert(tap(ui,ACTION_ROW,0).event==ReaderToolbarUi::Event::Row);
  if(!dense)assert(tap(ui,ACTION_CHOICE,9).event==ReaderToolbarUi::Event::Choice);
  model.panel=false;model.chapterTitle="Chapter";model.pageInfo="12/40 51%";model.progressPermille=500;
  ui.setModel(model);ui.render();assert(tap(ui,ACTION_PREV,0).event==ReaderToolbarUi::Event::PrevChapter);
  assert(tap(ui,ACTION_NEXT,0).event==ReaderToolbarUi::Event::NextChapter);
  assert((renderer.clip==std::array<int,4>{0,0,screenW,screenH}));++scenarios;
 }
 printf("GREEN %d non-X4 Cross/Ugly reader sheets, 3 tiers, 2 rotations, dense/icon choices and chapter routing\n",scenarios);
}
'''
(a.output/'x3.cpp').write_text(cpp)
sdk=a.repo/'freeink-sdk/libs/ui/FreeInkUI'
subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-I'+str(sdk/'include'),'-I'+str(a.repo/'lib/I18n'),str(a.output/'x3.cpp'),str(sdk/'src/FreeInkUI.cpp'),'-o',str(a.output/'x3')],check=True)
subprocess.run([str(a.output/'x3')],check=True)
