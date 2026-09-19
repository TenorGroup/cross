#pragma once
#include <memory>
#include <string>
#include <vector>
namespace freeink::ui {
struct ListItem { const char* label=nullptr; int actionValue=0; };
struct ListProps { ListItem* items=nullptr; int count=0,action=0; struct Text {int maxLines=0;} labelText; };
}
class GfxRenderer {};
class MappedInputManager {};
class UiScreen {
 public:
  std::vector<std::string> labels;
  void setContentMarginFromScreen(std::initializer_list<int16_t>) {}
  int theme(){return 0;}
  void list(freeink::ui::ListProps p){for(int i=0;i<p.count;++i) labels.push_back(p.items[i].label);}
};
class UiListActivity {
 public:
  UiListActivity(const char*,GfxRenderer& r,MappedInputManager& i):renderer(r),mappedInput(i){}
  virtual ~UiListActivity()=default;
  virtual void onEnter(){}
  void click(int index){activateIndex(index);}
  int rowCount() const {return listCount();}
  std::vector<std::string> labels(){UiScreen s;buildScreen(s);return s.labels;}
 protected:
  virtual const char* headerTitle() const=0;
  virtual int listCount() const=0;
  virtual void buildScreen(UiScreen&)=0;
  virtual void activateIndex(int)=0;
  virtual void drawChrome(){}
  struct RenderLock { explicit RenderLock(UiListActivity&){} };
  struct Nav { void reset(){} } nav;
  GfxRenderer& renderer;MappedInputManager& mappedInput;
  static constexpr int ACTION_ROW=1;
  void requestUpdate(){}
  void syncListViewport(UiScreen&,freeink::ui::ListProps&){}
  template<class T> void startActivityForResult(std::unique_ptr<T>,std::nullptr_t){}
};
