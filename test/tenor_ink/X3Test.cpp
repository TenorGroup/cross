#include <Arduino.h>
#include <HalStorage.h>
#include <JPEGDEC.h>
#include <JpegToBmpConverter.h>
#include "TenorInk.h"
#include "GrayThumb.h"
#include <HeapFloor.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <new>
#include <set>
#include <string>
#include <vector>

EspClass ESP;
namespace {
struct Block { void* p; size_t n; };
Block blocks[64]{};
bool measuring = false;
size_t call = 0, failAt = size_t(-1), dropAt = size_t(-1), live = 0, peak = 0;
std::multiset<size_t> bigTaken, bigJudged;
std::set<std::string> doors;
const char* refuseWho = nullptr;
bool refused = false;
void* take(size_t n) noexcept {
  if (measuring && call++ == failAt) return nullptr;
  void* p = std::malloc(n);
  if (p && measuring && n >= heapfloor::kBigBytes) bigTaken.insert(n);
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
// The heap floor door on the device reads the internal heap after the block; here it reads the stub.
namespace heapfloor {
bool judge(size_t bytes, const void*, const char* who) {
  if (measuring) { doors.insert(who); if (bytes >= kBigBytes) bigJudged.insert(bytes); }
  if (measuring && refuseWho && std::strcmp(who, refuseWho) == 0) { refused = true; return false; }
  return stands(bytes, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}
void* take(size_t bytes, const char* who) {
  void* p = std::malloc(bytes);
  if (p && !judge(bytes, p, who)) { std::free(p); return nullptr; }
  return p;
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
  // Every block of 4 KB or more went through the heap floor door, each under its own name.
  assert(bigTaken==bigJudged && !bigTaken.empty());
  const size_t judged=bigJudged.size(), taken=bigTaken.size();
  const char* names[]={"ink-jpeg","ink-mcu","ink-bounds","ink-rows","ink-errors"};
  for (const char* who : names) assert(doors.count(who));
  for (const char* who : names) {
    call=live=peak=0;refuseWho=who;refused=false;measuring=true;
    bool ok;
    { HalFile file(bytes);Output rejected;ok=JpegToBmpConverter::jpegFileToInkBmpStream(file,rejected,w,h,levels==4); }
    measuring=false;refuseWho=nullptr;
    assert(refused && !ok && live==0);
  }
  std::printf("GREEN heap floor door: %zu of %zu blocks of 4 KB or more judged, 5 names refused cleanly\n",judged,taken);
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
    ESP.freeHeap=largest?200000:10239;ESP.largestHeap=largest?8179:200000;
    HalFile file(bytes);Output rejected;assert(!JpegToBmpConverter::jpegFileToInkBmpStream(file,rejected,w,h,levels==4));
    ESP.freeHeap=ESP.largestHeap=200000;
  }
  // On the floor itself (free 10,240 B, largest reported 8,180 B) the cover is drawn.
  {
    ESP.freeHeap=10240;ESP.largestHeap=8180;
    HalFile file(bytes);Output edge;assert(JpegToBmpConverter::jpegFileToInkBmpStream(file,edge,w,h,levels==4));
    assert(edge.b==expected.b);
    ESP.freeHeap=ESP.largestHeap=200000;
  }
  std::printf("GREEN clean allocation failures=%zu, post-floor=2, pre-floor=2\n",allocations);
  // The JPEG decoder of the screen cover (jpeg-decoder) and of the Home thumbnail (jpeg-thumb) goes through the
  // heap floor door: refused, the conversion returns false and frees everything. The thumbnail is 40 px high,
  // under the fixture's 1/8 progressive grid (a thumbnail does not upscale).
  {
    call=live=peak=0;doors.clear();measuring=true;
    bool ok;
    { HalFile file(bytes);Output bmp;ok=JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(file,bmp,w,h); }
    { HalFile file(bytes);GrayThumb thumb(40);int scale=0;ok=ok&&JpegToBmpConverter::jpegFileToGrayThumb(file,thumb,&scale); }
    measuring=false;
    assert(ok && doors.count("jpeg-decoder") && doors.count("jpeg-thumb") && live==0);
    for (const char* who : {"jpeg-decoder","jpeg-thumb"}) {
      const bool decoderRefused=std::strcmp(who,"jpeg-decoder")==0;
      call=live=peak=0;refuseWho=who;refused=false;measuring=true;
      bool cover,thumbOk;
      { HalFile file(bytes);Output bmp;cover=JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(file,bmp,w,h); }
      { HalFile file(bytes);GrayThumb thumb(40);int scale=0;thumbOk=JpegToBmpConverter::jpegFileToGrayThumb(file,thumb,&scale); }
      measuring=false;refuseWho=nullptr;
      assert(refused && cover!=decoderRefused && thumbOk==decoderRefused && live==0);
    }
    std::puts("GREEN jpeg-decoder and jpeg-thumb pass the heap floor door and unwind when refused");
  }
  if(argc>2) {
    std::ofstream out(argv[2],std::ios::binary);out.write(reinterpret_cast<char*>(actual.b.data()),actual.b.size());
    std::ofstream pgm(std::string(argv[2])+".pgm",std::ios::binary);
    pgm<<"P5\n"<<sw<<" "<<sh<<"\n255\n";pgm.write(reinterpret_cast<char*>(pixels.data()),pixels.size());
  }
}
