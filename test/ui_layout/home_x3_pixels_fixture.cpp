#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#include <GfxRenderer.h>
#include <SdCardFont.h>
#include <cstdlib>
#include <Bitmap.h>
#include <HalDisplay.h>
#include <HalStorage.h>
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
SdCardFont* SdCardFont::fromMissCtx(void* ctx) { if(ctx) std::abort(); return nullptr; }
int SdCardFont::prewarm(const char*,uint8_t,bool,bool,bool) { std::abort(); }
uint16_t SdCardFont::getAdvance(uint32_t,uint8_t) const { std::abort(); }
uint8_t SdCardFont::resolveStyle(uint8_t) const { std::abort(); }
bool SdCardFont::hasAdvanceTable() const { std::abort(); }
bool SdCardFont::isOverflowGlyph(const EpdGlyph*) const { std::abort(); }
const uint8_t* SdCardFont::getOverflowBitmap(const EpdGlyph*) const { std::abort(); }
constexpr int SMALL_FONT_ID=0, UI_10_FONT_ID=1, UI_12_FONT_ID=2;
struct Settings { int uiTextSize=0; } SETTINGS;
namespace tenorchrome { constexpr bool kTouchShell=false; }
enum class StrId { STR_RECENT_STAT_READ, STR_RECENT_STAT_FINISH, STR_RECENT_STAT_TOTAL,
                   STR_RECENT_STAT_AVERAGE, STR_RECENT_STAT_DAYS, STR_STATS_TURNS };
const char* LABELS[]={"Đã đọc", "Dự kiến xong", "Tổng thời gian", "Trung bình", "Số ngày đọc", "Lượt lật trang"};
struct Locale { const char* get(StrId id) { return LABELS[static_cast<int>(id)]; } } I18N;
std::vector<uint8_t> pixels(528*792/8,255);
HalDisplay::HalDisplay()=default;
HalDisplay::~HalDisplay()=default;
uint8_t* HalDisplay::getFrameBuffer() const { return pixels.data(); }
uint16_t HalDisplay::getDisplayWidth() const { return 528; }
uint16_t HalDisplay::getDisplayHeight() const { return 792; }
uint16_t HalDisplay::getDisplayWidthBytes() const { return 66; }
uint32_t HalDisplay::getBufferSize() const { return pixels.size(); }
void HalDisplay::displayBuffer(RefreshMode,bool) {}
void HalDisplay::clearScreen(uint8_t c) const { std::fill(pixels.begin(),pixels.end(),c); }
void HalDisplay::cleanupGrayscaleBuffers(const uint8_t*) {}
void HalDisplay::displayGrayscaleBase(RefreshMode,bool) {}
void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t*) {}
void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t*) {}
void HalDisplay::displayGrayBuffer(bool) {}
bool HalDisplay::isInverted() const { return false; }
#define LOG_INF(...)
@@BOUNDS@@
struct HomeActivity {
  GfxRenderer& renderer;
  struct { uint8_t rows=0, percent=15; bool recorded=true; std::string values[HOME_STAT_COUNT]; } cardStats;
  struct CoverSize { int width=0,height=0; };
  CoverSize cardCoverSize(int) { return {}; }
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
  int statLabelFont=SETTINGS.uiTextSize==2?SMALL_FONT_ID:UI_10_FONT_ID;
  int statValueFont=SETTINGS.uiTextSize==2?UI_10_FONT_ID:UI_12_FONT_ID;
  @@PLACE@@
  return frame;
}
int main() {
  const EpdFontData* regular[]={&geist_8_regular,&geist_10_regular,&geist_12_regular,&geist_14_regular,&geist_16_regular};
  const EpdFontData* bold[]={&geist_8_regular,&geist_10_bold,&geist_12_bold,&geist_14_bold,&geist_16_bold};
  HalDisplay display; GfxRenderer renderer(display); renderer.begin(); renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  for(int tier=0;tier<3;++tier) {
    SETTINGS.uiTextSize=tier;
    std::vector<EpdFont> fonts;fonts.reserve(6);
    for(int id=0;id<3;++id) { fonts.emplace_back(regular[tier+id]); fonts.emplace_back(bold[tier+id]); renderer.insertFont(id,EpdFontFamily(&fonts[id*2],&fonts[id*2+1])); }
    for(int record=0;record<3;++record) {
      std::fill(pixels.begin(),pixels.end(),255);
      HomeActivity h{renderer}; h.cardStats.rows=record==0?63:record==1?5:1; h.cardStats.recorded=record<2;
      const char* vals[]={"15%","25/01","10h 8m","2h 1m/ngày","5","24"};
      for(int row=0;row<HOME_STAT_COUNT;++row) h.cardStats.values[row]=vals[row];
      if(record==2) h.cardStats.values[0]="Chưa có số liệu";
      HomeCardInput in;in.screenWidth=528;in.top=tier==0?125:tier==1?137:147;in.bottom=tier==0?752:tier==1?712:698;
      in.titleLineHeight=tier?43:38;in.titleLines=2;in.authorLineHeight=renderer.getLineHeight(UI_10_FONT_ID);
      in.excerptLineHeight=tier==0?34:tier==1?39:45;in.rowLineHeight=in.authorLineHeight;in.metadataAboveCover=tier>0;
      in.statsMinWidth=tier==0?160:tier==1?228:233;
      in.minStatsHeight=2+6*(renderer.getLineHeight(tier==2?SMALL_FONT_ID:UI_10_FONT_ID)+renderer.getLineHeight(tier==2?UI_10_FONT_ID:UI_12_FONT_ID))+12;
      auto card=h.cardLayout(in);
      for(int y=0;y<card.coverH;++y)for(int x=0;x<card.coverW;++x)renderer.drawPixel(card.coverX+x,card.coverY+y,((x/7+y/11)&1)==0);
      renderer.maskRoundedRectOutsideCorners(card.coverX,card.coverY,card.coverW,card.coverH,8);
      h.drawCardStats(card);
      renderer.drawText(UI_12_FONT_ID,card.textX,card.titleY,"Sách ở màn hình Home",true,EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID,card.textX,card.authorY,"Tenor");
      renderer.drawLine(40,card.ruleY,488,card.ruleY);
      renderer.drawText(UI_10_FONT_ID,40,card.rowY,"Cuốn khác");
      const std::string name="tier"+std::to_string(tier)+"-record"+std::to_string(record)+".pbm";
      std::ofstream out(name,std::ios::binary);out<<"P4\n528 792\n";
      for(auto pixel:pixels) out.put(static_cast<char>(~pixel));
      printf("X3 tier=%d record=%d cover=%d,%d,%d,%d stats=%d,%d\n",tier,record,card.coverX,card.coverY,card.coverW,card.coverH,card.statsX,card.statsRight);
    }
    for(int id=0;id<3;++id) renderer.removeFont(id);
  }
}
