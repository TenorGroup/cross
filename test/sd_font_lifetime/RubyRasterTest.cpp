#include "RasterFixture.h"
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  std::filesystem::create_directories(argv[1]);
  size_t frames = 0;
  for (int device = 0; device < 2; ++device) {
    width = device ? 800 : 792; height = device ? 480 : 528;
    pixels.resize(width / 8 * height);
    for (int tier = 0; tier < 3; ++tier) {
      Face regular(tier, 0), bold(tier, 1), italic(tier, 2), boldItalic(tier, 3);
      HalDisplay display; GfxRenderer renderer(display); renderer.begin();
      renderer.insertFont(42, EpdFontFamily(&regular.font, &bold.font, &italic.font, &boldItalic.font));
      for (int rotation = 0; rotation < 4; ++rotation) {
        renderer.setOrientation(static_cast<GfxRenderer::Orientation>(rotation));
        for (int mode = 0; mode < 3; ++mode) {
          renderer.setRenderMode(static_cast<GfxRenderer::RenderMode>(mode));
          for (int boundary : {0, 80, 160}) {
            std::fill(pixels.begin(), pixels.end(), 0xff);
            rasterBlockCalls = rasterAdvanceCalls = 0;
            drawCorpus(renderer, boundary);
            std::printf("RUBY_CALLS d=%d tier=%d rotation=%d mode=%d y=%d render=%lu advance=%lu\n",
                        device, tier, rotation, mode, boundary, rasterBlockCalls, rasterAdvanceCalls);
            if (std::all_of(pixels.begin(), pixels.end(), [](auto b) { return b == 0xff; })) return 3;
            const std::string name = std::string(argv[1]) + "/d" + std::to_string(device) + "-t" +
                std::to_string(tier) + "-r" + std::to_string(rotation) + "-m" + std::to_string(mode) +
                "-y" + std::to_string(boundary) + ".bin";
            std::ofstream out(name, std::ios::binary);
            out.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
            if (!out) return 4;
            ++frames;
          }
        }
      }
    }
  }
  std::printf("RUBY_RASTER frames=%zu\n", frames);
}
