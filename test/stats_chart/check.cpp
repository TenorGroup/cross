#include <GfxRenderer.h>
#include <I18n.h>
#include <ReadingStatsStore.h>
#include <cstdio>
#include "components/ReadingStatsView.h"
#include "components/ReadingStatsLayout.h"
int failures=0,checks=0;
void check(bool ok,const char* msg) {++checks;if(!ok){++failures;printf("FAIL %s\n",msg);}}
int main(int argc,char**) {
 if(argc>1) {
  for(uint8_t tier:{0,1,2})for(int width:{480,528})for(int locale:{0,1,2}) {
   SETTINGS.uiTextSize=tier;statsLocale=locale;GfxRenderer r;r.width=width;
   readingstatsview::drawSleep(r);
   printf("scenario %u %d %d\n",tier,width,locale);
   for(const auto& a:r.runs)printf("text %d %d %d %d %s\n",a.x,a.y,a.width,a.height,a.text.c_str());
   for(const auto& a:r.dots)printf("dot %d %d\n",a.x,a.y);
   for(const auto& a:r.lines)printf("line %d %d %d %d\n",a.x1,a.y1,a.x2,a.y2);
   for(const auto& a:r.rectangles)printf("rect %d %d %d %d\n",a.x,a.y,a.w,a.h);
  }
  return 0;
 }
 for(uint8_t tier:{0,1,2}) for(int width:{480,528}) for(int locale:{0,1,2}) for(int extra:{0,1}) for(bool compact:{false,true}) {
  SETTINGS.uiTextSize=tier;statsLocale=locale;
  const int pages=tier?2:1;
  for(int page=0;page<pages;++page) {
   GfxRenderer r;r.width=width;r.subtitleExtra=extra;
   READING_STATS.kho.days={{20260920,50,1000,20},{20260919,20,0,10},{20260918,0,0,0}};
   readingstatsview::draw(r,162,false,compact,tier?page:-1);
   int panels=0,bars=0;
   for(const auto& shape:r.shapes) {
    if(!shape.fill&&shape.line==1) {++panels;check(shape.x==24&&shape.w==width-48,"panel side margin");}
    if(shape.fill) {++bars;check(shape.radius<=std::min(shape.w,shape.h)/2,"fitted bar radius");}
    check(shape.y>=162&&shape.y+shape.h<=792,"shape bounds");
    if(tier==0)check(shape.y+shape.h<=162+(compact?390:430),"default badge budget");
   }
   check(panels==(tier?1:2),"rounded panels");
   if(page==0) {
    check(bars==2,"zero and missing never positive bars");
    check(r.lines.size()==1 && r.dots.size()==4,"known zero dash differs from missing dot");
    const HomeReadingStatsLayout layout(r.getLineHeight(SMALL_FONT_ID),r.getLineHeight(UI_10_FONT_ID),r.getLineHeight(UI_12_FONT_ID),compact,tier!=0);
    int dates=0;
    for(const auto& a:r.runs)if(a.y==162+layout.dates&&a.text.size()==2&&a.text[0]>='0'&&a.text[0]<='9'&&a.text[1]>='0'&&a.text[1]<='9') {
     const int center=36+(width-72)*(2*dates+1)/14;
     check(a.x+a.width/2==center,"date aligned with column");++dates;
    }
    check(dates==7,"exact 7 dates");
    std::vector<int> heights;for(const auto& a:r.shapes)if(a.fill)heights.push_back(a.h);
    if(heights.size()==2)check(heights[0]==1200*heights[1]/3001,"bar ratio from actual seconds");
   }
   for(size_t i=0;i<r.runs.size();++i) {
    const auto& a=r.runs[i];if(!(a.x>=36&&a.x+a.width<=width-36))printf("bounds tier=%u width=%d locale=%d page=%d text=%s x=%d w=%d\n",tier,width,locale,page,a.text.c_str(),a.x,a.width);check(a.x>=36&&a.x+a.width<=width-36,"text side bounds");
    for(size_t j=0;j<i;++j){const auto& b=r.runs[j];if(a.x<b.x+b.width&&b.x<a.x+a.width&&a.y<b.y+b.height&&b.y<a.y+a.height)printf("overlap tier=%u width=%d locale=%d page=%d %s/%s\n",tier,width,locale,page,a.text.c_str(),b.text.c_str());check(!(a.x<b.x+b.width&&b.x<a.x+a.width&&a.y<b.y+b.height&&b.y<a.y+a.height),"text overlap");}
   }
   const int declared=readingstatsview::panelHeight(r,tier?page:-1);
   int bottom=162;for(const auto& shape:r.shapes)if(!shape.fill&&shape.line==1)bottom=std::max(bottom,shape.y+shape.h);
   if(!compact||tier)check(declared==bottom-162,"panelHeight matches drawing");
  }
 }
 {
  SETTINGS.uiTextSize=0;statsLocale=0;GfxRenderer r;r.width=528;
  READING_STATS.kho.days={{20260920,50,1000,20},{20260919,20,0,10},{20260918,0,0,0}};
  readingstatsview::draw(r,162);
  for(const char* expected:{"50 phút","1 giờ 10 phút","35 phút","2 ngày","23","40%","5 / 3"}) {
   bool found=false;for(const auto& text:r.runs)if(text.text==expected)found=true;check(found,"source numbers preserved");
  }
 }
 printf("%d checks, %d failures\n",checks,failures);return failures?1:0;
}
