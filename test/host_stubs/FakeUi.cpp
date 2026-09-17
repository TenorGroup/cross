// Ban gia lap tang dieu huong, hien thi va cham, du de DUNG mot man tren may ban.
// Chi dinh nghia dung nhung ky hieu trinh lien ket doi, do bang phep thu ngay
// 13/09/2026: 21 ky hieu. Khong ban nao thay doi hanh vi cua man dang kiem.
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>

#include <cstring>
#include <string>
#include <vector>

#include "HostTestDraw.h"
#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "components/UITheme.h"
#include "components/UiAppHost.h"

// Dieu huong: bai kiem chi quan tam man hien tai, nen moi loi goi la khong lam gi.
void ActivityManager::goToReader(std::string, bool) {}
void ActivityManager::goHome(HomeMenuItem, bool) {}
void ActivityManager::pushActivity(std::unique_ptr<Activity>&&) {}
void ActivityManager::popActivity() {}
bool ActivityManager::switchSettingsSibling(int) { return false; }
void ActivityManager::requestUpdate(bool) {}
void ActivityManager::requestUpdateAndWait() {}

// Bo ve: moi lenh ve la khong lam gi. Dung nguon that thi no keo tiep sang tang
// font, bitmap va chu hai chieu, tuc cai duoi cu dai ra; gia o day cat dut chuoi.
// Bai kiem dau vao khong doc diem anh, chi doc trang thai sau moi lan bam.
void GfxRenderer::clearScreen(unsigned char) const {}
void GfxRenderer::displayBuffer(HalDisplay::RefreshMode) const {}
int GfxRenderer::getScreenWidth() const { return 528; }
int GfxRenderer::getScreenHeight() const { return 792; }
void GfxRenderer::tapToLogical(float, float, int& outX, int& outY) const {
  outX = 0;
  outY = 0;
}
void GfxRenderer::getOrientedViewableTRBL(int* t, int* r, int* b, int* l) const {
  if (t) *t = 0;
  if (r) *r = 0;
  if (b) *b = 0;
  if (l) *l = 0;
}
void GfxRenderer::drawPixel(int, int, bool) const {}
void GfxRenderer::drawLine(int, int, int, int, int, bool) const {}
void GfxRenderer::drawLine(int, int, int, int, bool) const {}
void GfxRenderer::drawRect(int, int, int, int, int, bool) const {}
void GfxRenderer::drawRoundedRect(int, int, int, int, int, int, bool) const {}
void GfxRenderer::drawRoundedRect(int, int, int, int, int, int, bool, bool, bool, bool, bool) const {}
void GfxRenderer::fillRect(int, int, int, int, bool) const {}
void GfxRenderer::fillRectDither(int, int, int, int, Color) const {}
void GfxRenderer::fillRoundedRect(int, int, int, int, int, Color) const {}
void GfxRenderer::fillRoundedRect(int, int, int, int, int, bool, bool, bool, bool, Color) const {}
void GfxRenderer::fillPolygon(const int*, const int*, int, bool) const {}
namespace hosttest {
std::vector<DrawnText>& drawn() {
  static std::vector<DrawnText> v;
  return v;
}
void clearDrawn() { drawn().clear(); }
bool wasDrawn(const std::string& text) {
  for (const auto& d : drawn()) {
    if (d.text == text) return true;
  }
  return false;
}
}  // namespace hosttest

void GfxRenderer::drawText(int, const int x, const int y, const char* text, bool, EpdFontFamily::Style,
                           BidiUtils::BidiBaseDir, int, uint8_t) const {
  if (text != nullptr) {
    hosttest::drawn().push_back({text, x, y, false});
  }
}
void GfxRenderer::drawTextRotated90CW(int, const int x, const int y, const char* text, bool,
                                      EpdFontFamily::Style) const {
  if (text != nullptr) {
    hosttest::drawn().push_back({text, x, y, true});
  }
}
// Do chu: 8 diem anh moi ky tu, 20 diem anh moi dong. Con so phai khac 0 de phep
// tinh bo cuc khong chia cho 0; gia tri chinh xac khong anh huong bai kiem nut.
int GfxRenderer::getTextWidth(int, const char* text, EpdFontFamily::Style, BidiUtils::BidiBaseDir, int) const {
  return text ? static_cast<int>(strlen(text)) * 8 : 0;
}
int GfxRenderer::getLineHeight(int) const { return 20; }
std::string GfxRenderer::truncatedText(int, const char* text, int, EpdFontFamily::Style, int) const {
  return text ? std::string(text) : std::string();
}
std::vector<std::string> GfxRenderer::wrappedText(int, const char* text, int, int, EpdFontFamily::Style) const {
  return {text ? std::string(text) : std::string()};
}

RenderLock::RenderLock(Activity&) {}
RenderLock::~RenderLock() {}

const ThemeMetrics& UITheme::getMetrics() const {
  static const ThemeMetrics metrics{};
  return metrics;
}

// UiAppHost dung nguon THAT: no co san file rieng, gia lai re hon dung.

// Doi tuong nen ma Arduino khai la toan cuc.
EspClass ESP;

// Ba doi tuong toan cuc ma ma nguon that khai bang extern, binh thuong do main.cpp
// dinh nghia. Bai kiem khong chay main.cpp nen dinh nghia o day.
// ActivityManager doi hai tham chieu, va ham huy cua no assert(false) vi tren may
// that no khong bao gio bi huy. Bo kiem dung che do phat hanh (NDEBUG) nen assert
// bi go bo, va doi tuong nay song toi het chuong trinh nhu tren may that.
// HalDisplay is never constructed: it holds the panel driver, and building one
// would pull that whole stack in. GfxRenderer only binds the reference and every
// draw call here is a no-op, so the storage is never read.
alignas(HalDisplay) unsigned char displayStorage[sizeof(HalDisplay)];
HalDisplay& fakeDisplay = reinterpret_cast<HalDisplay&>(displayStorage);
GfxRenderer fakeRenderer(fakeDisplay);
HalGPIO fakeGpio;
MappedInputManager fakeInput(fakeGpio, fakeRenderer);

ActivityManager activityManager(fakeRenderer, fakeInput);
HalFrontlight HalFrontlight::instance;
UITheme UITheme::instance;

void GfxRenderer::freeBwBufferChunks() {}
UITheme::UITheme() {}

// Ham ve chay vo tan tren may that; bai kiem khong bao gio goi toi.
[[noreturn]] void ActivityManager::renderTaskLoop() {
  for (;;) {
  }
}

// Cho bai kiem muon vung nho hien thi, de no dung duoc GfxRenderer that.
HalDisplay& hostTestDisplay() { return fakeDisplay; }

// The input harness stubs pixel rendering; simulator journeys cover the header.
HalGPIO gpio;
namespace tenorchrome {
int tipY(const GfxRenderer&) { return 726; }
int tipHeight(const GfxRenderer&, const char*, int) { return 28; }
void drawTip(const GfxRenderer&, const char*, int, int) {}
void drawHeader(const GfxRenderer&, const char*, const char*) {}
}  // namespace tenorchrome

// Persist only in memory in the input harness; simulator tests cover SD failures.
#include "MenuCustomization.h"
namespace menucustom {
State& state() {
  static State data;
  return data;
}
void load() {}
bool failSave = false;
bool save() { return !failSave; }
}  // namespace menucustom

void GfxRenderer::drawCenteredText(int, int, const char*, bool, EpdFontFamily::Style, BidiUtils::BidiBaseDir) const {}
