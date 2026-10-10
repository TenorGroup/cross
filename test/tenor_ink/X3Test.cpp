#include <Arduino.h>
#include <HalStorage.h>
#include <JPEGDEC.h>
#include <JpegToBmpConverter.h>
#include "TenorInk.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <new>
#include <vector>

EspClass ESP;
namespace {
struct Block { void* p; size_t n; };
Block blocks[64]{};
bool measuring = false;
size_t call = 0, failAt = size_t(-1), dropAt = size_t(-1), live = 0, peak = 0;
void* take(size_t n) noexcept {
  if (measuring && call++ == failAt) return nullptr;
  void* p = std::malloc(n);
  if (p && measuring && call - 1 == dropAt) ESP.freeHeap = 10239;
  if (p && measuring) {
    for (auto& b : blocks) if (!b.p) { b = {p,n}; live += n; peak = std::max(peak,live); return p; }
    std::abort();
  }
  return p;
}
void release(void* p) noexcept {
  for (auto& b : blocks) if (b.p == p && p) { live -= b.n; b = {}; break; }
  std::free(p);
}
struct Output : Print {
  std::vector<uint8_t> b;
  size_t write(uint8_t v) override { b.push_back(v); return 1; }
  size_t write(const uint8_t* p, size_t n) override { b.insert(b.end(),p,p+n); return n; }
};
std::vector<uint8_t> pixels;
int sw,sh;
int capture(JPEGDRAW* d) {
  for (int y=0;y<d->iHeight && d->y+y<sh;++y)
    std::memcpy(pixels.data()+size_t(d->y+y)*sw+d->x,
                reinterpret_cast<uint8_t*>(d->pPixels)+y*d->iWidth,std::min(d->iWidthUsed,sw-d->x));
  return 1;
}
}
void* operator new(size_t n,const std::nothrow_t&) noexcept { return take(n); }
void* operator new[](size_t n,const std::nothrow_t&) noexcept { return take(n); }
void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete(void* p,size_t) noexcept { release(p); }
void operator delete[](void* p,size_t) noexcept { release(p); }
void operator delete(void* p,const std::nothrow_t&) noexcept { release(p); }
void operator delete[](void* p,const std::nothrow_t&) noexcept { release(p); }

int main(int argc,char** argv) {
  const char* input = argc > 1 ? argv[1] : FIXTURES "/generated.jpg";
  std::ifstream f(input,std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
  assert(!bytes.empty());
  JPEGDEC decoder;
  assert(decoder.openRAM(bytes.data(),int(bytes.size()),capture));
  const bool progressive=decoder.getJPEGType()==JPEG_MODE_PROGRESSIVE;
  const int scale=progressive?8:1;
  sw=(decoder.getWidth()+scale-1)/scale;sh=(decoder.getHeight()+scale-1)/scale;
  pixels.resize(size_t(sw)*sh);
  decoder.setPixelType(EIGHT_BIT_GRAYSCALE);
  assert(decoder.decode(0,0,progressive?JPEG_SCALE_EIGHTH:0));decoder.close();
  const int w=argc>3?std::atoi(argv[3]):166,h=argc>4?std::atoi(argv[4]):250;
  const int levels=argc>5?std::atoi(argv[5]):4;
  Output expected,actual;
  assert(tenorink::writeCover(pixels.data(),sw,sh,expected,w,h,levels==4));
  call=live=peak=0;measuring=true;
  { HalFile file(bytes);assert(JpegToBmpConverter::jpegFileToInkBmpStream(file,actual,w,h,levels==4)); }
  measuring=false;
  assert(actual.b==expected.b && live==0);
  const size_t allocations=call;
  std::printf("GREEN grid=%dx%d frame=%dx%d levels=%d peak_internal_nothrow=%zu allocations=%zu\n",sw,sh,w,h,levels,peak,allocations);
  for (size_t i=0;i<allocations;++i) {
    call=live=peak=0;failAt=i;measuring=true;
    bool ok;
    { HalFile file(bytes);Output rejected;ok=JpegToBmpConverter::jpegFileToInkBmpStream(file,rejected,w,h,levels==4); }
    measuring=false;
    assert(!ok && live==0);
  }
  failAt=size_t(-1);
  for (size_t i=0;i<2;++i) {
    call=live=peak=0;dropAt=i;measuring=true;
    { HalFile file(bytes);Output rejected;assert(!JpegToBmpConverter::jpegFileToInkBmpStream(file,rejected,w,h,levels==4)); }
    measuring=false;dropAt=size_t(-1);ESP.freeHeap=200000;
    assert(live==0);
  }
  for (bool largest : {false,true}) {
    ESP.freeHeap=largest?200000:10240;ESP.largestHeap=largest?8180:200000;
    HalFile file(bytes);Output rejected;assert(!JpegToBmpConverter::jpegFileToInkBmpStream(file,rejected,w,h,levels==4));
    ESP.freeHeap=ESP.largestHeap=200000;
  }
  std::printf("GREEN clean allocation failures=%zu, post-floor=2, pre-floor=2\n",allocations);
  if(argc>2) {
    std::ofstream out(argv[2],std::ios::binary);out.write(reinterpret_cast<char*>(actual.b.data()),actual.b.size());
    std::ofstream pgm(std::string(argv[2])+".pgm",std::ios::binary);
    pgm<<"P5\n"<<sw<<" "<<sh<<"\n255\n";pgm.write(reinterpret_cast<char*>(pixels.data()),pixels.size());
  }
}
