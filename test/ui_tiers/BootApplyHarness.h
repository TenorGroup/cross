#pragma once
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <GfxRenderer.h>
#include <FontCacheManager.h>
#include "UIFontTiers.h"
#include "SdCardFontSystem.h"
#include "components/UIScale.h"
#include "fontIds.h"
#define FREEINK_MCU_C3 1
#define OMIT_FONTS 1
#define LOG_INF(...) ((void)0)
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)

int uiTestLockDepth = 0;
bool uiTestBootRegistration = false;
class RenderLock {
 public:
  RenderLock() { ++uiTestLockDepth; }
  ~RenderLock() { --uiTestLockDepth; }
};
struct { uint8_t uiTextSize = 0; uint8_t sleepScreen = 0; } SETTINGS;
struct CrossPointSettings { enum { UGLY = 11 }; };
namespace shell { inline bool isUgly() { return false; } }
struct {
  bool begun = false;
  void begin(bool) { begun = true; }
} display;
struct {
  bool workerStarted = false;
  void begin() { assert(display.begun); workerStarted = true; }
} activityManager;
struct { bool init() { return true; } } fontDecompressor;
GfxRenderer renderer;
namespace ugly { inline void ensureFonts(GfxRenderer&) {} }
FontCacheManager fontCacheManager;
SdCardFontSystem sdFontSystem;
int fallbackRefreshCount = 0;
int fallbackTier = -1;
bool sdBegun = false;
void logHeapMark(const char*) {}
SdCardFontManager::~SdCardFontManager() = default;
// Out of line since the vector-font path (#3646); nothing to own without PSRAM.
SdCardFontSystem::SdCardFontSystem() = default;
SdCardFontSystem::~SdCardFontSystem() = default;
void SdCardFontSystem::begin(GfxRenderer&) { assert(uiTestLockDepth > 0); sdBegun = true; }
void SdCardFontSystem::refreshUiFallbacks(GfxRenderer& target, uint8_t size) {
  assert(uiTestLockDepth > 0);
  if (uiTestBootRegistration) assert(sdBegun);
  assert(!target.getFontCacheManager() || target.getFontCacheManager()->clearCount > 0);
  ++fallbackRefreshCount;
  fallbackTier = size;
}
EpdFontData captionData = [] { EpdFontData data{}; data.advanceY = 21; return data; }();
EpdFontData subtitleData = [] { EpdFontData data{}; data.advanceY = 26; return data; }();
EpdFontData bodyData = [] { EpdFontData data{}; data.advanceY = 33; return data; }();
EpdFont captionFont(&captionData), subtitleFont(&subtitleData), bodyFont(&bodyData);
EpdFontFamily smallFontFamily(&captionFont), ui10FontFamily(&subtitleFont), ui12FontFamily(&bodyFont);
EpdFontFamily notoserif14FontFamily(&bodyFont);
void setupDisplayAndFonts(bool, bool);

void assertTier(GfxRenderer& target, uint8_t tier) {
  const auto spec = uiTextSizeSpec(tier);
  assert(target.getFontMap().at(SMALL_FONT_ID).getData()->advanceY == spec.captionLineHeight);
  assert(target.getFontMap().at(UI_10_FONT_ID).getData()->advanceY == spec.subtitleLineHeight);
  assert(target.getFontMap().at(UI_12_FONT_ID).getData()->advanceY == spec.bodyLineHeight);
}
void firstPaint(uint8_t savedTier) {
  assert(uiTestLockDepth == 0 && activityManager.workerStarted && sdBegun);
  assertTier(renderer,savedTier);
  assert(fallbackTier == normalizedUiTextSize(savedTier));
  assert(fontCacheManager.clearCount == 1);
}
void testApply() {
  GfxRenderer target;
  FontCacheManager cache;
  target.setFontCacheManager(&cache);
  constexpr int readerId = 888;
  target.insertFont(readerId,notoserif14FontFamily);
  auto* reader = reinterpret_cast<SdCardFont*>(static_cast<uintptr_t>(0x1000));
  target.registerSdCardFont(readerId,reader);
  target.insertFont(SMALL_FONT_ID,smallFontFamily);
  target.insertFont(UI_10_FONT_ID,ui10FontFamily);
  const auto* captionNode = &target.getFontMap().at(SMALL_FONT_ID);
  const auto* readerNode = &target.getFontMap().at(readerId);
  const auto* readerData = readerNode->getData();
  const int initialRefresh = fallbackRefreshCount;
  RenderLock lock;
  // Missing final alias: earlier aliases must stay untouched and no cache I/O occurs.
  assert(!applyUiFontSize(target,2));
  assert(cache.clearCount == 0 && fallbackRefreshCount == initialRefresh);
  assert(captionNode->getData() == &captionData);
  target.insertFont(UI_12_FONT_ID,ui12FontFamily);
  target.registerSdCardFont(UI_12_FONT_ID,reader);
  assert(!applyUiFontSize(target,2));
  assert(cache.clearCount == 0 && fallbackRefreshCount == initialRefresh);
  assert(captionNode->getData() == &captionData);
  target.removeFont(UI_12_FONT_ID);
  target.insertFont(UI_12_FONT_ID,ui12FontFamily);
  for (const uint8_t tier : {0,1,2,0,255}) {
    const int before = cache.clearCount;
    assert(applyUiFontSize(target,tier));
    assert(cache.clearCount == before+1);
    assertTier(target,tier);
    assert(fallbackTier == normalizedUiTextSize(tier));
    assert(&target.getFontMap().at(SMALL_FONT_ID) == captionNode);
    assert(&target.getFontMap().at(readerId) == readerNode);
    assert(readerNode->getData() == readerData && target.isSdCardFont(readerId));
  }
  // Direct renderer API also rejects absent and SD IDs without inserting nodes.
  const auto count = target.getFontMap().size();
  assert(!target.replaceBuiltinFont(12345,smallFontFamily));
  assert(!target.replaceBuiltinFont(readerId,smallFontFamily));
  assert(target.getFontMap().size() == count && readerNode->getData() == readerData);
}
int main(int argc,char** argv) {
  assert(argc == 2);
  if (std::string(argv[1]) == "apply") {
    testApply();
    std::puts("PASS: production apply preflight, cache clear, tier/reader/node invariants");
  } else {
    const int saved = std::atoi(argv[1]); assert(saved >= 0 && saved <= 2);
    SETTINGS.uiTextSize = static_cast<uint8_t>(saved);
    uiTestBootRegistration = true;
    setupDisplayAndFonts(false, true);
    firstPaint(SETTINGS.uiTextSize);
    std::printf("PASS: persisted tier %d applied under lock before first paint\n",saved);
  }
}
