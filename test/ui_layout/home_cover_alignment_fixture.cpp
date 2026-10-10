#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>
#include <EpdFont.h>
#include "components/CompactStats.h"
#include "components/HomeExcerptStyle.h"
#include <builtinFonts/geist_8_regular.h>
#include <builtinFonts/geist_10_regular.h>
#include <builtinFonts/geist_10_bold.h>
#include <builtinFonts/geist_12_regular.h>
#include <builtinFonts/geist_12_bold.h>
#include <builtinFonts/geist_14_regular.h>
#include <builtinFonts/geist_14_bold.h>
#include <builtinFonts/geist_16_regular.h>
#include <builtinFonts/geist_16_bold.h>
constexpr int SMALL_FONT_ID=0, UI_10_FONT_ID=1, UI_12_FONT_ID=2;
struct EpdFontFamily {
  enum Style { REGULAR, BOLD };
  const EpdFontData* regular; const EpdFontData* bold;
  const EpdFontData* getData(Style style) const { return style==BOLD?bold:regular; }
  const EpdGlyph* getGlyph(uint32_t cp,Style style) const { return EpdFont(getData(style)).getGlyph(cp); }
};
struct Settings { int uiTextSize=0; } SETTINGS;
namespace tenorchrome { bool kTouchShell=true; }
enum class Color { Black };
enum class StrId { STR_RECENT_STAT_READ, STR_RECENT_STAT_FINISH, STR_RECENT_STAT_TOTAL,
                   STR_RECENT_STAT_AVERAGE, STR_RECENT_STAT_DAYS, STR_STATS_TURNS };
const char* LABELS[]={"Đã đọc", "Dự kiến xong", "Tổng thời gian", "Trung bình", "Số ngày đọc", "Lượt lật trang"};
struct Locale { const char* get(StrId id) { return LABELS[static_cast<int>(id)]; } } I18N;
struct GfxRenderer {
  int getScreenWidth() const { return 480; }
  const EpdFontData* regular[5]={&geist_8_regular,&geist_10_regular,&geist_12_regular,&geist_14_regular,&geist_16_regular};
  const EpdFontData* bold[5]={&geist_8_regular,&geist_10_bold,&geist_12_bold,&geist_14_bold,&geist_16_bold};
  int inkTop=10000, inkBottom=0, inkLeft=10000, inkRight=0, barRight=-1;
  std::vector<int> paint;
  const EpdFontData* face(int font, EpdFontFamily::Style style=EpdFontFamily::REGULAR) const {
    return (style==EpdFontFamily::BOLD ? bold : regular)[font+SETTINGS.uiTextSize];
  }
  std::map<int,EpdFontFamily> getFontMap() const {
    return {{0,{face(0),face(0,EpdFontFamily::BOLD)}},{1,{face(1),face(1,EpdFontFamily::BOLD)}},{2,{face(2),face(2,EpdFontFamily::BOLD)}}};
  }
  const uint8_t* getGlyphBitmap(const EpdFontData* data,const EpdGlyph* glyph) const { return data->bitmap+glyph->dataOffset; }
  int getLineHeight(int font) const { return face(font)->advanceY; }
  int getFontAscenderSize(int font) const { return face(font)->ascender; }
  int getTextWidth(int font,const char* text,EpdFontFamily::Style style=EpdFontFamily::REGULAR) const {
    int w,h; EpdFont(face(font,style)).getTextDimensions(text,&w,&h); return w;
  }
  int getTextAdvanceX(int font,const char* text,EpdFontFamily::Style style) const {
    EpdFont f(face(font,style)); const auto* p=reinterpret_cast<const unsigned char*>(text);
    int x=0; uint32_t prev=0; const EpdGlyph* g=nullptr;
    while (auto cp=utf8NextCodepoint(&p)) { if(prev) x+=fp4::toPixel(g->advanceX+f.getKerning(prev,cp)); g=f.getGlyph(cp); prev=cp; }
    return x+(g ? fp4::toPixel(g->advanceX) : 0);
  }
  int getTextInkTop(int font,const char* text,EpdFontFamily::Style style) const {
    EpdFont f(face(font,style)); int result=face(font)->ascender; const auto* p=reinterpret_cast<const unsigned char*>(text);
    while(auto cp=utf8NextCodepoint(&p)) { auto* g=f.getGlyph(cp); if(g&&g->width&&g->height) result=std::min(result,int(face(font)->ascender-g->top)); }
    return result;
  }
  int getTextInkBottom(int font,const char* text,EpdFontFamily::Style style) const {
    EpdFont f(face(font,style)); int result=0; const auto* p=reinterpret_cast<const unsigned char*>(text);
    while(auto cp=utf8NextCodepoint(&p)) { auto* g=f.getGlyph(cp); if(g&&g->width&&g->height) result=std::max(result,int(face(font)->ascender-g->top+g->height)); }
    return result;
  }
  std::vector<std::string> wrappedText(int font,const char* text,int width,int lines) const {
    std::vector<std::string> out; std::string line;
    for (std::string word, remaining=text; !remaining.empty();) {
      auto split=remaining.find(' '); word=remaining.substr(0,split); remaining=split==std::string::npos ? "" : remaining.substr(split+1);
      auto next=line.empty()?word:line+" "+word;
      if(!line.empty() && getTextWidth(font,next.c_str())>width) { out.push_back(line); line=word; } else line=next;
    }
    if(!line.empty()) out.push_back(line); if(int(out.size())>lines) out.resize(lines); return out;
  }
  void drawText(int font,int x,int y,const char* text,bool=true,EpdFontFamily::Style style=EpdFontFamily::REGULAR) {
    inkTop=std::min(inkTop,y+getTextInkTop(font,text,style)); inkBottom=std::max(inkBottom,y+getTextInkBottom(font,text,style));
    inkLeft=std::min(inkLeft,x); inkRight=std::max(inkRight,x+getTextWidth(font,text,style));
    paint.insert(paint.end(),{font,x,y,int(style),getTextWidth(font,text,style)});
  }
  void drawRoundedRect(int x,int y,int w,int h,int,int,bool) { barRight=x+w-1; paint.insert(paint.end(),{x,y,w,h}); }
  void fillRoundedRect(int,int,int,int,int,Color) {}
  void reset() { inkTop=inkLeft=10000; inkBottom=inkRight=0; barRight=-1; paint.clear(); }
};
@@BOUNDS@@
#define LOG_INF(...)
struct HomeActivity {
  GfxRenderer renderer;
  struct { uint8_t rows=0, percent=15; bool recorded=true; std::string values[HOME_STAT_COUNT]; } cardStats;
  struct CoverSize { int width=0,height=0; } size;
  CoverSize cardCoverSize(int) { return size; }
  int cardThumbHeight=HOME_CARD_COVER_H;
  int cardThumbWidth=HOME_CARD_COVER_W;
  HomeCardLayout cardLayout(HomeCardInput in);
  void rememberContextLine(int, int, int, const char*, EpdFontFamily::Style = EpdFontFamily::REGULAR) {}
  size_t contextTextLineCount = 0;
  void rememberContextStatGroup(size_t, int, int, int) {}
  int drawCardStats(const HomeCardLayout& card, HomeStatsBounds* bounds=nullptr);
};
@@METHODS@@
HomeCardLayout HomeActivity::cardLayout(HomeCardInput in) {
    int shown=0;
    int statLabelFont=SETTINGS.uiTextSize > 0 ? SMALL_FONT_ID : UI_10_FONT_ID;
    int statValueFont=SETTINGS.uiTextSize > 0 ? UI_10_FONT_ID : UI_12_FONT_ID;
    @@PLACE@@
    return frame;
  }

int main() {
  int fails=0, count=0;
  int fullW[3][2] = {}, fullH[3][2] = {};
  for(int tier=0;tier<3;++tier) for(int record=0;record<3;++record) for(bool landscape:{false,true}) {
    SETTINGS.uiTextSize=tier; tenorchrome::kTouchShell=true;
    HomeActivity h;
    h.cardStats.rows=record==0?63:record==1?5:1; h.cardStats.recorded=record<2;
    const char* vals[]={"15%","25/01","10h 8m","2h 1m/ngày","5","24"};
    for(int row=0;row<HOME_STAT_COUNT;++row) h.cardStats.values[row]=vals[row];
    if(record==2) h.cardStats.values[0]="Chưa có số liệu";
    HomeCardInput in; in.screenWidth=480; in.top=44; in.bottom=tier==0?700:tier==1?688:677;
    in.titleLineHeight=tier?43:38; in.titleLines=2; in.authorLineHeight=h.renderer.getLineHeight(UI_10_FONT_ID);
    in.excerptLineHeight=tier==0?34:tier==1?39:45; in.rowLineHeight=in.authorLineHeight; in.rowLines=2; in.metadataAboveCover=tier>0;
    in.statsMinWidth=tier==0?147:tier==1?160:184;
    in.minStatsHeight=2+6*(h.renderer.getLineHeight(UI_10_FONT_ID)+h.renderer.getLineHeight(UI_12_FONT_ID))+12+2*std::max(0,2*h.renderer.getLineHeight(UI_10_FONT_ID)-h.renderer.getLineHeight(UI_12_FONT_ID));
    h.size={landscape?1200:298,landscape?800:450};
    auto card=h.cardLayout(in);
    const int sourceW=landscape?1200:298, sourceH=landscape?800:450;
    HomeStatsBounds bounds;
    @@MEASURE@@
    @@ALIGN@@
    h.renderer.reset(); h.drawCardStats(card);
    const auto firstInk=cardStatInkBounds(h.renderer,UI_10_FONT_ID,LABELS[0],EpdFontFamily::REGULAR);
    const int actualTop=card.statsTop+2+firstInk.top;
    const bool forced=card.coverH<bounds.bottom-bounds.top;
    const bool aligned=forced ? std::abs((2*card.coverY+card.coverH)-(bounds.top+bounds.bottom))<=1 :
                               card.coverY==bounds.top && card.coverY+card.coverH==bounds.bottom;
    int measuredMinWidth=0;
    int labelFont=tier>0?SMALL_FONT_ID:UI_10_FONT_ID, valueFont=tier>0?UI_10_FONT_ID:UI_12_FONT_ID;
    for(int row=0;row<HOME_STAT_COUNT;++row) {
      measuredMinWidth=std::max(measuredMinWidth,h.renderer.getTextWidth(labelFont,LABELS[row]));
      measuredMinWidth=std::max(measuredMinWidth,cardStatValueWidth(h.renderer,valueFont,row==HOME_STAT_AVERAGE?"2h 1m/ngày":row==HOME_STAT_FINISH?">365 ngày":"10h 8m"));
    }
    measuredMinWidth=std::min(measuredMinWidth,240);
    const int height=bounds.bottom-bounds.top;
    const int limit=464-24-22-measuredMinWidth;
    const int fullWidth=(height*sourceW+sourceH-1)/sourceH;
    const int expectedWidth=std::min(fullWidth,limit);
    const int expectedHeight=fullWidth>limit?limit*sourceH/sourceW:height;
    const bool titleFitsLeftColumn = landscape ? card.textW == card.coverW : card.textW == 440;
    bool ok=aligned && (landscape || !forced) && titleFitsLeftColumn && h.renderer.inkBottom<card.ruleY && card.coverW==expectedWidth && card.coverH==expectedHeight &&
            card.coverX==24 && card.statsX-card.coverX-card.coverW==22 &&
            h.renderer.inkRight<=464 && h.renderer.inkLeft>=card.statsX &&
            (!h.cardStats.recorded || h.renderer.barRight==463);
    if (record == 0) {
      if (!landscape) ok = ok && std::abs(h.renderer.inkBottom - (card.coverY + card.coverH)) <= 1;
      fullW[tier][landscape] = card.coverW;
      fullH[tier][landscape] = card.coverH;
    } else if (record == 1) {
      ok = ok && card.coverW == fullW[tier][landscape] && card.coverH == fullH[tier][landscape];
      ok = ok && h.renderer.inkBottom - actualTop < card.coverH;
    }
    if(!ok) ++fails;
    ++count;
    std::printf("%s tier=%d record=%d landscape=%d cover=%d,%d,%d,%d ink=%d..%d column=%d..%d bar=%d forced=%d\n",
      ok?"GREEN":"RED",tier,record,landscape,card.coverX,card.coverY,card.coverW,card.coverH,
      h.renderer.inkTop,h.renderer.inkBottom,card.statsX,h.renderer.inkRight-1,h.renderer.barRight,forced);
  }
  std::printf("cases=%d failed=%d\n",count,fails);
  return fails?1:0;
}
