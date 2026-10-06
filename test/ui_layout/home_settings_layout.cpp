#include "RecordingTarget.h"
#include "components/HomeSettingsLayout.h"
#include <EpdFont.h>
#include "builtinFonts/geist_12_regular.h"
#include "builtinFonts/geist_14_regular.h"
#include "builtinFonts/geist_16_regular.h"

#include <algorithm>
#include <set>

struct FontTarget : Target {
  EpdFont font;
  explicit FontTarget(const uint8_t tier) : Target(tier),
      font(tier == 0 ? &geist_12_regular : tier == 1 ? &geist_14_regular : &geist_16_regular) {}
  fui::Size measureText(fui::FontId, const char* text, fui::TextStyle) const override {
    int width=0,height=0;
    font.getTextDimensions(text,&width,&height);
    return {static_cast<int16_t>(width),spec.bodyLineHeight};
  }
};

int main() {
  int permutations = 0, paints = 0;
  std::vector<int> groups{0,1,2,3,4,5,6,7,8};
  do {
    const auto order = homesettings::order(groups);
    assert(order.count == 10 && order.readingCount == 5);
    std::vector<int> reading, machine{0};
    for (int i = 0; i < static_cast<int>(groups.size()); ++i)
      (homesettings::readingGroup(groups[i]) ? reading : machine).push_back(i + 1);
    reading.insert(reading.end(), machine.begin(), machine.end());
    for (int display = 0; display < order.count; ++display) {
      assert(order.original(display) == reading[display]);
      assert(order.display(order.original(display)) == display);
    }
    assert(order.original(-1) == -1 && order.original(10) == -1 && order.display(11) == -1);
    ++permutations;
  } while (std::next_permutation(groups.begin(), groups.end()));

  // X4 Pro: 3 titled groups whatever the stored order; File Transfer is -1, About & updates 99.
  for (bool motion : {false,true}) {
    std::vector<int> ids{6,8,5,4,3,2,1,7,0};
    if (!motion) ids.erase(ids.begin()+1);
    const auto order = homesettings::touchOrder(ids);
    std::vector<int> shown;
    for (int i = 0; i < order.count; ++i) {
      const int original = order.original(i);
      shown.push_back(original == 0 ? -1 : original > static_cast<int>(ids.size()) ? 99 : ids[original-1]);
    }
    const std::vector<int> want = motion ? std::vector<int>{-1,0,1,2, 3,4,5,8, 7,6,99}
                                         : std::vector<int>{-1,0,1,2, 3,4,5, 7,6,99};
    assert(shown == want);
    assert(order.heading(0) == 0 && order.heading(4) == 1 && order.heading(motion ? 8 : 7) == 2);
    assert(order.heading(1) == -1 && order.heading(order.count - 1) == -1);
  }

  for (bool touch : {false,true}) for (int tier : {0,1,2}) for (bool motion : {false,true}) {
    groups = {0,7,1,2,3,4,5,6};
    if (motion) groups.insert(groups.begin()+4,8);
    const auto order = homesettings::order(groups);
    const int heights[] = {623,571,551}, touchHeights[] = {678,671,666};
    FontTarget target(tier);
    const homesettings::Rect body{0,touch?38:129,touch?480:528,touch?touchHeights[tier]:heights[tier]};
    const auto geometry = homesettings::layout(body,order,touch,tier,target.lineHeight(1));
    if (touch && motion && tier==2) {
      assert(!geometry.fits);
      continue;
    }
    assert(geometry.fits);
    assert(geometry.frames[1].y - (geometry.frames[0].y + geometry.frames[0].height) == 16);
    assert(geometry.frames[1].y + geometry.frames[1].height <= body.y + body.height);
    for (int group = 0; group < 2; ++group) {
      assert(geometry.rows[group].x - geometry.frames[group].x >= 4);
      assert(geometry.rows[group].y - geometry.frames[group].y >= 4);
      assert(geometry.frames[group].height - geometry.rows[group].height >= 8);
    }
    fui::DeviceContext device;
    device.width=body.width; device.height=body.y+body.height+40; device.hasTouch=touch;
    fui::InteractionBuffer<16> interactions;
    fui::InputSnapshot input;
    fui::Frame<16> frame(target,device,input,interactions);
    const char* labels[]={"Hiển thị","Trình đọc","Điều khiển","Hệ thống","Thiết bị","Bàn phím","Khác","Ngủ","Cử chỉ"};
    fui::ListItem items[10];
    for (int i=0;i<order.count;++i) {
      const int original=order.original(i);
      items[i].label=original==0?"Truyền tệp":labels[groups[original-1]];
      items[i].actionValue=i; items[i].opensNext=true;
    }
    if (!touch || !motion) {
      int widest=0;
      for (int i=0;i<order.count;++i)
        widest=std::max(widest,static_cast<int>(target.measureText(1,items[i].label,{}).width));
      printf("%s tier%d rows%d: body%d row%d gap%d widest label%dpx\n",touch?"X4Pro":"X3",tier,order.count,
             body.height,geometry.rowHeight,geometry.rowGap,widest);
    }
    for (int selected=0;selected<=order.count;++selected) {
      interactions.clear();
      int offset=0;
      for (int group=0;group<2;++group) {
        const int count=group==0?order.readingCount:order.count-order.readingCount;
        fui::ListProps props;
        props.items=items+offset; props.count=count; props.action=42;
        props.inputMask=fui::InputTouch|fui::InputLongPress;
        props.labelText.font=1; props.labelText.maxLines=2;
        props.rowHeight=geometry.rowHeight; props.rowGap=geometry.rowGap;
        props.rowPaddingY=geometry.rowPaddingY; props.sidePadding=12; props.rowInset=0;
        props.scrollIndicator=false; props.selectedIndex=touch?-1:selected-1-offset;
        const auto rect=geometry.rows[group];
        for (int i=0;i<count;++i) {
          const auto measured=fui::measureListRow(target,nullptr,rect.width,props,items[offset+i]);
          assert(measured.labelLines==1 && measured.height==geometry.rowHeight);
        }
        fui::list(frame,{static_cast<int16_t>(rect.x),static_cast<int16_t>(rect.y),
                         static_cast<int16_t>(rect.width),static_cast<int16_t>(rect.height)},props);
        offset+=count;
      }
      std::set<int> values;
      for (size_t i=0;i<interactions.count();++i) {
        const auto& hit=interactions.data()[i];
        assert(hit.rect.y >= body.y && hit.rect.bottom() <= body.y+body.height);
        values.insert(hit.value);
      }
      assert(values.size()==static_cast<size_t>(order.count));
      for (int i=0;i<order.count;++i) assert(values.count(i)==1);
      ++paints;
    }
    const auto shortBody=homesettings::Rect{0,0,body.width,100};
    assert(!homesettings::layout(shortBody,order,touch,tier,target.lineHeight(1)).fits);
  }
  printf("PASS: %d group-order permutations; %d actual SDK two-block paints, all rows/actions intact\n",permutations,paints);
}
