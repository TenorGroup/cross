#include <GfxRenderer.h>
#include <HalDisplay.h>
#include "components/ContextMenuBackground.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>

std::vector<uint8_t> pixels(HalDisplay::BUFFER_SIZE, 0xff);
HalDisplay::HalDisplay() = default;
HalDisplay::~HalDisplay() = default;
uint8_t* HalDisplay::getFrameBuffer() const { return pixels.data(); }
uint16_t HalDisplay::getDisplayWidth() const { return DISPLAY_WIDTH; }
uint16_t HalDisplay::getDisplayHeight() const { return DISPLAY_HEIGHT; }
uint16_t HalDisplay::getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
uint32_t HalDisplay::getBufferSize() const { return BUFFER_SIZE; }
void HalDisplay::displayBuffer(RefreshMode, bool) {}
void HalDisplay::clearScreen(uint8_t c) const { std::fill(pixels.begin(), pixels.end(), c); }
void HalDisplay::cleanupGrayscaleBuffers(const uint8_t*) {}
void HalDisplay::displayGrayscaleBase(RefreshMode, bool) {}
void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t*) {}
void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t*) {}
void HalDisplay::displayGrayBuffer(bool) {}
bool HalDisplay::isInverted() const { return false; }

bool ink(int x, int y) {
  return !(pixels[y * HalDisplay::DISPLAY_WIDTH_BYTES + x / 8] & (0x80 >> (x % 8)));
}

int main(int argc, char**) {
  HalDisplay display; GfxRenderer renderer(display); renderer.begin();
  struct Rect {int x=0,y=0,width=0,height=0;int bottom()const{return y+height;}int right()const{return x+width;}};
  int cases=0;
  for(auto orientation:{GfxRenderer::Portrait,GfxRenderer::PortraitInverted,GfxRenderer::LandscapeCounterClockwise}) {
    renderer.setOrientation(orientation);
    for(int menuY:{180,240,300}) {
      renderer.clearScreen();
      std::array<Rect,4> covers{{{24,70,120,180},{176,420,128,150},{},{}}};
      for(const auto& c:covers)if(c.width)renderer.fillRect(c.x,c.y,c.width,c.height,true);
      // Two complete text lines cut by the top and bottom edges of the halo.
      renderer.fillRect(170,menuY-15,240,22,true);
      renderer.fillRect(24,menuY+128,400,22,true);
      renderer.fillRect(24,menuY-42,400,10,true);
      const Rect menu{17,menuY,290,136};
      if(argc!=2)clearContextMenuLines(renderer,menu,covers,38,650);
      DirectPixelWriter pen;pen.init(renderer);int bits=pen.displayWidthBytes*8;
      auto pixel=[&](int x,int y){int bit=(pen.phyYBase+x*pen.phyYStepX+y*pen.phyYStepY)*bits+pen.phyXBase+x*pen.phyXStepX+y*pen.phyXStepY;return !(pen.fb[bit/8]&(0x80>>(bit%8)));};
      for(int y=menuY-15;y<menuY+150;++y)for(int x=16;x<renderer.getScreenWidth()-16;++x){
        bool art=false;for(const auto& c:covers)art=art||(c.width&&x>=c.x-7&&x<c.right()+7&&y>=c.y-7&&y<c.bottom()+7);
        if(!art)assert(!pixel(x,y));
      }
      assert(pixel(60,120));assert(pixel(200,480));
      assert(pixel(200,menuY-40));
      ++cases;
    }
  }
  renderer.setOrientation(GfxRenderer::Portrait);
  renderer.clearScreen();
  std::array<Rect, 4> lines{{{280, 218, 200, 25}, {24, 310, 480, 24},
                            {280, 472, 160, 22}, {280, 180, 160, 22}}};
  for (const auto& line : lines) renderer.fillRect(line.x, line.y, line.width, line.height, true);
  // The halo cuts just 1 px of the first and third lines; the fourth stays intact.
  const Rect dialog{70, 250, 370, 215};
  if (argc != 3) clearContextMenuTextRects(renderer, dialog, lines.data(), lines.size());
  DirectPixelWriter rectPen; rectPen.init(renderer);
  const int rectBits = rectPen.displayWidthBytes * 8;
  const auto rectInk = [&](int x, int y) {
    const int bit = (rectPen.phyYBase + x * rectPen.phyYStepX + y * rectPen.phyYStepY) * rectBits +
                     rectPen.phyXBase + x * rectPen.phyXStepX + y * rectPen.phyXStepY;
    return !(rectPen.fb[bit / 8] & (0x80 >> (bit % 8)));
  };
  for (int i = 0; i < 3; ++i) {
    const auto& line = lines[i];
    for (int y = line.y; y < line.bottom(); ++y)
      for (int x = line.x; x < line.right(); ++x) assert(!rectInk(x, y));
  }
  assert(rectInk(300, 190));
  // A label, wrapped value and progress bar form 1 cell, whichever part the halo touches.
  for (int menuY : {210, 270, 335}) {
    renderer.clearScreen();
    std::array<Rect, 4> cells{{{276, 218, 139, 25}, {276, 274, 18, 21},
                              {276, 301, 50, 21}, {276, 390, 154, 23}}};
    const Rect progress{276, 333, 220, 8};
    for (const auto& cell : cells) renderer.fillRect(cell.x, cell.y, cell.width, cell.height, true);
    renderer.fillRect(progress.x, progress.y, progress.width, progress.height, true);
    size_t count = 3;
    if (argc != 4) mergeContextMenuTextRects(cells.data(), count, size_t{0}, progress);
    cells[count++] = {276, 390, 154, 23};
    clearContextMenuTextRects(renderer, Rect{100, menuY, 250, 20}, cells.data(), count);
    assert(!rectInk(280, 225));
    assert(!rectInk(280, 280));
    assert(!rectInk(280, 310));
    assert(!rectInk(450, 337));
    assert(rectInk(280, 400));
  }
  printf("GREEN %d complete text-band clears, cover art retained, 3 orientations\n",cases);
}
