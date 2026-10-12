#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <cassert>
#include <fstream>
#include <cstdio>
#include <InflateReader.h>
#include "components/X3BrandCodec.h"
#include "components/X4ProBrandAssets.h"
#include "components/X3BrandScreen.h"
#include "activities/boot_sleep/SleepGrayPlanes.h"
bool renderX3BrandScreen(GfxRenderer&, bool);
extern size_t brandDecoderAllocations;
HalGPIO gpio;
SleepGrayPlanes::~SleepGrayPlanes() = default;
void SleepGrayPlanes::lsb() { renderer.copyGrayscaleLsbBuffers(); }
void SleepGrayPlanes::show() { renderer.copyGrayscaleMsbBuffers(); renderer.displayGrayBuffer(); }
int main(int argc, char** argv) {
 assert(argc == 3);
 std::string mode = argv[1];
 gpio.x3 = mode == "x3" || mode == "x3-sleep";
 const bool sleep = mode == "x4-sleep" || mode == "x3-sleep";
 GfxRenderer renderer(gpio.x3 ? 528 : 480, gpio.x3 ? 792 : 800);
 if (mode == "x4-combined") renderer.combined = true;
 if (mode == "wrong-orientation") renderer.width = 800;
 if (mode == "no-gray") renderer.available = false;
 if (mode == "wrong-size") renderer.framebuffer.resize(1);
 bool ready = RENDER_BRAND(renderer, !sleep);
 if (mode == "no-gray" || mode == "wrong-size" || mode == "wrong-orientation" || mode == "x4-disabled") {
   assert(!ready && renderer.gray == 0 && renderer.bases == 0);
 } else {
   assert(ready);
   // Sleep: no version line; the X3 alone runs a GC pass of the art first (ghost clear), the X4 Pro
   // sleeps through its base refresh as its other gray sleep screens do.
   assert(renderer.gray == 1 && renderer.bases == 1);
   assert(renderer.full == (mode == "x3-sleep" ? 1 : 0));
   assert(renderer.versions == (sleep ? 0 : renderer.combined ? 2 : 3));
   for (auto part : {"lsb", "msb"}) {
     const auto& data = std::string(part) == "lsb" ? renderer.lsb : renderer.msb;
     std::ofstream out(std::string(argv[2]) + "/" + mode + "-" + part + ".bin", std::ios::binary);
     out.write(reinterpret_cast<const char*>(data.data()), data.size());
   }
 }
 assert(renderer.orientation == GfxRenderer::LandscapeClockwise);
 if (mode == "x4") {
   assert(brandDecoderAllocations == 0);
   std::printf("Decoder heap=0, sizeof(InflateReader)=%zu bytes\n", sizeof(InflateReader));
   auto bad = std::vector<uint8_t>(std::begin(x4probrand::BOOT_LSB), std::end(x4probrand::BOOT_LSB));
   bad.back() ^= 1;
   assert(!decodeX3BrandPlane(bad.data(), bad.size(), renderer.framebuffer.data(), renderer.framebuffer.size(), true));
   assert(!decodeX3BrandPlane(x4probrand::BOOT_LSB, sizeof(x4probrand::BOOT_LSB)-1, renderer.framebuffer.data(), renderer.framebuffer.size(), true));
 }

}
