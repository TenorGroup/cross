// Real renderer callback, plane-copy and clipped strip paths; HAL uploads stay in RAM.
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <SdCardFont.h>
#include <algorithm>
#include <cassert>
#include <vector>

namespace {
std::vector<uint8_t> pixels(HalDisplay::BUFFER_SIZE,0xff),uploaded;
int baseShows=0,grayCopies=0,hooks=0;
void decoration(const GfxRenderer& r) {
  ++hooks;
  // Pure BW decoration for the base; a gray overlay needs a clear mask.
  if(r.getRenderMode()==GfxRenderer::BW || r.grayPlanesAreAbsolute()) {
    r.fillRect(100,80,80,60,false);
    r.drawRect(112,92,12,36);
  } else r.fillRect(100,80,80,60,true);
}
}
HalDisplay::HalDisplay()=default;
HalDisplay::~HalDisplay()=default;
uint8_t* HalDisplay::getFrameBuffer() const { return pixels.data(); }
uint16_t HalDisplay::getDisplayWidth() const { return DISPLAY_WIDTH; }
uint16_t HalDisplay::getDisplayHeight() const { return DISPLAY_HEIGHT; }
uint16_t HalDisplay::getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
uint32_t HalDisplay::getBufferSize() const { return BUFFER_SIZE; }
void HalDisplay::displayGrayscaleBase(RefreshMode,bool) { ++baseShows; uploaded=pixels; }
bool HalDisplay::displayGrayscaleBase(GrayscaleMode,RefreshMode,bool) { ++baseShows; uploaded=pixels; return true; }
void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t*) { ++grayCopies; uploaded=pixels; }
void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t*) { ++grayCopies; uploaded=pixels; }
void HalDisplay::cleanupGrayscaleBuffers(const uint8_t*) {}
void SdCardFont::clearCache() {}

int main() {
  HalDisplay display;
  GfxRenderer r(display);
  r.begin();
  GfxRenderer::preDisplayHook=decoration;
  GfxRenderer::preGrayUploadHook=decoration;
  for(int orientation=0;orientation<4;++orientation) {
    r.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
    r.setRenderMode(GfxRenderer::BW);
    std::fill(pixels.begin(),pixels.end(),0);
    const int before=baseShows;
    r.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
    assert(baseShows==before+1);  // Decoration adds no waveform.
    const auto bw=uploaded;
    assert(std::any_of(bw.begin(),bw.end(),[](uint8_t b){return b!=0;}));
    for(auto mode:{GfxRenderer::GRAYSCALE_LSB,GfxRenderer::GRAYSCALE_MSB}) {
      r.setRenderMode(mode);
      std::fill(pixels.begin(),pixels.end(),0xff);
      const int copied=grayCopies;
      if(mode==GfxRenderer::GRAYSCALE_LSB) r.copyGrayscaleLsbBuffers(); else r.copyGrayscaleMsbBuffers();
      assert(grayCopies==copied+1);
      const auto full=uploaded;
      assert(std::any_of(full.begin(),full.end(),[](uint8_t b){return b!=0xff;}));
      // Every rotated strip must match the full plane, with guards untouched.
      for(int y=0;y<HalDisplay::DISPLAY_HEIGHT;y+=23) {
        const int rows=std::min(23,int(HalDisplay::DISPLAY_HEIGHT)-y);
        const int bytes=rows*HalDisplay::DISPLAY_WIDTH_BYTES;
        std::vector<uint8_t> scratch(bytes+32,0xa5);
        std::fill(scratch.begin()+16,scratch.end()-16,0xff);
        r.beginStripTarget(scratch.data()+16,y,rows);
        r.endStripTarget();
        assert(std::equal(scratch.begin()+16,scratch.end()-16,full.begin()+y*HalDisplay::DISPLAY_WIDTH_BYTES));
        for(int k=0;k<16;++k) assert(scratch[k]==0xa5 && scratch[bytes+16+k]==0xa5);
      }
    }
    r.setRenderMode(GfxRenderer::BW);
    std::fill(pixels.begin(),pixels.end(),0);
    r.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute,HalDisplay::FAST_REFRESH);
    assert(r.grayPlanesAreAbsolute());
    r.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    std::fill(pixels.begin(),pixels.end(),0);
    r.copyGrayscaleLsbBuffers();
    assert(uploaded==bw);  // Absolute endpoints match white/black base.
    // An ordinary BW strip never asks the gray-plane decorator to run.
    r.setRenderMode(GfxRenderer::BW);
    const int oldHooks=hooks;
    std::vector<uint8_t> scratch(HalDisplay::DISPLAY_WIDTH_BYTES,0xff);
    r.beginStripTarget(scratch.data(),0,1); r.endStripTarget();
    assert(hooks==oldHooks);
  }
}
