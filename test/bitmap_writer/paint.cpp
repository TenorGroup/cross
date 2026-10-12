#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <HeapFloor.h>
#include <BitmapHelpers.h>
template <typename... T> void quiet(T...) {}
#define LOG_DBG(...) quiet(__VA_ARGS__)
#define LOG_INF(...) quiet(__VA_ARGS__)
#define LOG_ERR(...) quiet(__VA_ARGS__)
static uint32_t micros() { return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static int pixelCalls = 0, rotations = 0;
enum class BmpReaderError { Ok, Error };
struct Bitmap {
  int w, h, bpp;
  bool top;
  mutable int row = 0;
  int getWidth() const { return w; }
  int getHeight() const { return h; }
  int getRowBytes() const { return (w*bpp+31)/32*4; }
  bool isTopDown() const { return top; }
  bool is1Bit() const { return bpp==1; }
  bool isBlackWhite() const { return false; }
  int getBpp() const { return bpp; }
  BmpReaderError readNextRow(uint8_t* out, uint8_t*) const {
    memset(out, 0, (w+3)/4);
    const int y=top?row:h-1-row;
    for(int x=0;x<w;++x) {
      int level=(x*17+y*31+(x*y)%13)%4;
      if(bpp==1) level=level<2?0:3;
      out[x/4]|=level<<(6-(x%4)*2);
    }
    ++row;
    return BmpReaderError::Ok;
  }
};
struct FontCache { bool isScanning() const { return false; } };
struct GfxRenderer {
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };
  enum Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };
  int panelWidth, panelHeight, panelWidthBytes;
  Orientation orientation=Portrait;
  RenderMode renderMode=BW;
  bool absoluteGrayPlanes=false;
  FontCache* fontCacheManager_=nullptr;
  int clipLeft_=0,clipTop_=0,clipRight_=32767,clipBottom_=32767;
  bool _stripActive=false;
  int _stripY0=0,_stripRows=0;
  mutable std::vector<uint8_t> buffer;
  uint8_t* frameBuffer;
  GfxRenderer(int w,int h):panelWidth(w),panelHeight(h),panelWidthBytes((w+7)/8),buffer((w+7)/8*h,0xa5),frameBuffer(buffer.data()){}
  int getScreenWidth() const {return orientation==Portrait||orientation==PortraitInverted?panelHeight:panelWidth;}
  int getScreenHeight() const {return orientation==Portrait||orientation==PortraitInverted?panelWidth:panelHeight;}
  uint8_t* getWriteTarget() const {return frameBuffer;}
  int getWriteOriginY() const {return _stripActive?_stripY0:0;}
  int getWriteRows() const {return _stripActive?_stripRows:panelHeight;}
  bool drawBitmapRows(const Bitmap&,int,int,int,int,int,int,bool) const {return false;}
  bool drawBitmap1Bit(const Bitmap&,int,int,int,int) const {return true;}
  void preserveImagePolarity(int,int,int,int) const {}
  void drawPixel(int x,int y,bool state=true) const;
  bool drawBitmap(const Bitmap&,int,int,int,int,float,float,const uint8_t(*)[4],bool) const;
  bool reference(const Bitmap&,int,int,int,int,float,float,const uint8_t(*)[4],bool) const;
};
static void rotateCoordinates(GfxRenderer::Orientation o,int x,int y,int* px,int* py,int w,int h) {
  ++rotations;
  switch(o) {
    case GfxRenderer::Portrait:*px=y;*py=h-1-x;break;
    case GfxRenderer::PortraitInverted:*px=w-1-y;*py=x;break;
    case GfxRenderer::LandscapeClockwise:*px=w-1-x;*py=h-1-y;break;
    default:*px=x;*py=y;break;
  }
}
void GfxRenderer::drawPixel(int x,int y,bool state) const {
  ++pixelCalls;
  if(x<clipLeft_||y<clipTop_||x>=clipRight_||y>=clipBottom_)return;
  int px,py;rotateCoordinates(orientation,x,y,&px,&py,panelWidth,panelHeight);
  if(px<0||px>=panelWidth||py<0||py>=panelHeight)return;
  if(_stripActive&&(py<_stripY0||py>=_stripY0+_stripRows))return;
  auto& byte=buffer[(py-getWriteOriginY())*panelWidthBytes+px/8];
  uint8_t mask=128>>(px%8);
  if(state)byte&=~mask;else byte|=mask;
}
#include "row.inc"
#include "paint.inc"
#include "reference.inc"
int main(int argc,char** argv) {
  const bool dither=argc>1&&std::strcmp(argv[1],"dither")==0;
  const bool referenceOnly=argc>2&&std::strcmp(argv[2],"reference")==0;
  const uint8_t patterns[4][4]={{0,0,0,0},{0x88,0x22,0x88,0x22},{0xaa,0x55,0xaa,0x55},{255,255,255,255}};
  int cases=0,errors=0; long long oldCalls=0,newCalls=0; double oldUs=0,newUs=0;
  for(int orientation=0;orientation<4;++orientation) for(int bpp:{1,2})
  for(int mode=0;mode<3;++mode) for(bool top:{false,true}) for(bool absolute:{false,true})
  for(bool strip:{false,true}) for(int crop:{0,1}) for(int offset:{-7,0,33})
  for(int scale:{0,1}) for(bool transparent:{false,true}) {
    // The general path also accepts 1-bit rows when a crop or dither is requested.
    if(bpp==1&&!crop&&!dither)continue;
    GfxRenderer a(792,528),b(792,528);
    for(auto* r:{&a,&b}) {
      r->orientation=static_cast<GfxRenderer::Orientation>(orientation);
      r->renderMode=static_cast<GfxRenderer::RenderMode>(mode);
      r->absoluteGrayPlanes=absolute;
      r->clipLeft_=9;r->clipTop_=7;r->clipRight_=153;r->clipBottom_=193;
      r->_stripActive=strip;r->_stripY0=41;r->_stripRows=83;
    }
    Bitmap src{177,211,bpp,top},ref=src;
    const float cx=crop?0.21f:0,cy=crop?0.13f:0;
    pixelCalls=rotations=0;auto t=micros();
    b.reference(ref,offset,offset,scale?89:0,scale?111:0,cx,cy,dither?patterns:nullptr,transparent);
    oldUs+=micros()-t;oldCalls+=pixelCalls;
    pixelCalls=rotations=0;t=micros();
    a.drawBitmap(src,offset,offset,scale?89:0,scale?111:0,cx,cy,dither?patterns:nullptr,transparent);
    newUs+=micros()-t;newCalls+=pixelCalls;
    if(a.buffer!=b.buffer){if(errors<8)std::printf("bytes differ o=%d bpp=%d mode=%d top=%d strip=%d crop=%d scale=%d\n",orientation,bpp,mode,top,strip,crop,scale);++errors;}
    ++cases;
  }
  std::printf("cases=%d byte_errors=%d drawPixel=%lld -> %lld time_ms=%.3f -> %.3f path=%s\n",cases,errors,oldCalls,newCalls,oldUs/1000,newUs/1000,dither?"dither":"plain");
  return errors||(!referenceOnly&&newCalls!=0)?1:0;
}
