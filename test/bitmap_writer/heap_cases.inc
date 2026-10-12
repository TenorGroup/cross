int main() {
  int errors=0,cases=0;
  const auto refusal=[&](const char* tag,int path,int bpp) {
    GfxRenderer r(792,528);
    Bitmap b{2048,20,bpp,true};
    if(path==3){b.w=528;b.h=792;r.absoluteGrayPlanes=true;}
    const auto before=r.buffer;
    deniedTag=tag;deniedCalls=0;pixelCalls=0;
    bool ok=false;
    switch(path) {
      case 0:ok=r.drawBitmap(b,0,0,180,0,0,0,nullptr,false);break;
      case 1:ok=r.drawBitmapCover(b,0,0,300,400);break;
      case 2:ok=r.drawBitmapRows(b,0,0,0,0,400,20,true);break;
      default:ok=r.drawBitmapAbsolutePlanes(b);break;
    }
    if(ok||b.row||r.buffer!=before||!deniedCalls){++errors;std::printf("FAIL refusal %s path=%d rows=%d gate=%d\n",tag,path,b.row,deniedCalls);}
    ++cases;deniedTag="";
  };
  refusal("bmp-row",0,2);refusal("bmp-columns",0,2);
  refusal("cover-pixels",1,2);refusal("cover-row",1,2);refusal("cover-columns",1,2);
  refusal("bmp-raw-row",2,1);
  refusal("gray-bitmap",3,2);refusal("bmp-gray-row",3,2);refusal("bmp-gray-levels",3,2);
  for(int denied=1;denied<=14;++denied) {
    GfxRenderer r(792,528);const auto before=r.buffer;
    takeCalls=judgeCalls=0;refuseJudgeAt=denied;
    const bool ok=r.storeBwBuffer();
    const bool empty=std::all_of(r.bwBufferChunks.begin(),r.bwBufferChunks.end(),[](auto* p){return p==nullptr;});
    if(ok||!empty||r.buffer!=before||takeCalls!=denied){++errors;std::printf("FAIL snapshot step=%d takes=%d judges=%d\n",denied,takeCalls,judgeCalls);}
    r.freeBwBufferChunks();++cases;
  }
  refuseJudgeAt=0;
  GfxRenderer r(792,528);const bool ok=r.storeBwBuffer();
  if(!ok)++errors;r.freeBwBufferChunks();
  std::printf("allocation_cases=%d errors=%d snapshot_chunks=%zu\n",cases,errors,r.bwBufferChunks.size());
  return errors?1:0;
}
