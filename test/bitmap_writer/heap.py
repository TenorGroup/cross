import argparse
from pathlib import Path
import subprocess
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--cxx',default='c++');p.add_argument('--source',type=Path);a=p.parse_args()
repo=Path(__file__).resolve().parents[2];here=Path(__file__).resolve().parent
a.output.mkdir(parents=True,exist_ok=True)
s=(a.source or repo/'lib/GfxRenderer/GfxRenderer.cpp').read_text()
base=(here/'paint.cpp').read_text().split('#include "row.inc"')[0]
base=base.replace('  bool isBlackWhite() const { return false; }', '')
base=base.replace('bool is1Bit() const { return bpp==1; }','''bool is1Bit() const { return bpp==1; }
  bool isBlackWhite() const { return bpp==1; }
  BmpReaderError rewindToData() const {row=0;return BmpReaderError::Ok;}
  BmpReaderError readRawRow(uint8_t* out) const {memset(out,0x55,getRowBytes());++row;return BmpReaderError::Ok;}''')
base=base.replace('mutable std::vector<uint8_t> buffer;', '''static constexpr size_t BW_BUFFER_CHUNK_SIZE=4000;
  size_t frameBufferSize=792*528/8;
  std::vector<uint8_t*> bwBufferChunks=std::vector<uint8_t*>((frameBufferSize+3999)/4000,nullptr);
  struct Display {void copyGrayscaleLsbBuffers(uint8_t*) const {} void copyGrayscaleMsbBuffers(uint8_t*) const {}} display;
  int getBufferSize() const {return frameBufferSize;}
  bool storeBwBuffer();
  void freeBwBufferChunks();
  bool drawBitmapCover(const Bitmap&,int,int,int,int) const;
  bool drawBitmapAbsolutePlanes(const Bitmap&) const;
  void drawBitRow(const uint8_t*,int,int,int,int,bool) const;
  mutable std::vector<uint8_t> buffer;''')
base=base.replace('bool drawBitmapRows(const Bitmap&,int,int,int,int,int,int,bool) const {return false;}', 'bool drawBitmapRows(const Bitmap&,int,int,int,int,int,int,bool) const;')
mock='''
static const char* deniedTag="";
static int refuseJudgeAt=0, judgeCalls=0, takeCalls=0, deniedCalls=0;
namespace testfloor {
inline bool judge(size_t,const void*,const char*) {return ++judgeCalls != refuseJudgeAt;}
inline void* take(size_t n,const char* tag) {
  ++takeCalls;
  if(!std::strcmp(tag,deniedTag)){++deniedCalls;return nullptr;}
  return malloc(n);
}
template<typename T> std::unique_ptr<T> array(size_t n,const char* tag) {
  if(!std::strcmp(tag,deniedTag)){++deniedCalls;return nullptr;}
  return heapfloor::array<T>(n,tag);
}
}
'''
funcs=[]
for signature,end in [
 ('bool GfxRenderer::drawBitmapAbsolutePlanes','\nbool GfxRenderer::drawBitmap('),
 ('bool GfxRenderer::drawBitmap(const Bitmap&','\nbool GfxRenderer::drawBitmap1Bit'),
 ('bool GfxRenderer::drawBitmapCover','\nvoid GfxRenderer::drawBits'),
 ('void GfxRenderer::freeBwBufferChunks','\n/**\n * This can only be called')]:
    begin=s.index(signature);funcs.append(s[begin:s.index(end,begin)].replace('heapfloor::','testfloor::'))
source=base+'\n#include "BitmapRow.h"\n'+mock+'\n'.join(funcs)+(here/'heap_cases.inc').read_text()
(a.output/'heap.cpp').write_text(source)
exe=a.output/'heap'
subprocess.run([a.cxx,'-std=c++20','-O1','-g','-fsanitize=address,undefined','-Wall','-Wextra','-Werror',
 '-I'+str(repo/'lib/GfxRenderer'),'-I'+str(repo/'lib/Memory'),str(a.output/'heap.cpp'),'-o',str(exe)],check=True)
raise SystemExit(subprocess.run([str(exe)]).returncode)
