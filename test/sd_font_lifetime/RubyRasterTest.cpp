#include RUBY_BLOCK_HEADER
#include <GfxRenderer.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

unsigned long rasterBlockCalls = 0, rasterAdvanceCalls = 0;
namespace {
unsigned width = 792, height = 528;
std::vector<uint8_t> pixels;
struct Face {
  std::vector<uint8_t> bitmap;
  std::vector<EpdGlyph> glyphs;
  std::vector<EpdUnicodeInterval> intervals;
  EpdFontData data{};
  EpdFont font;
  Face(int tier, int variant) : font(&data) {
    std::vector<uint32_t> cps;
    for (uint32_t cp = 32; cp < 127; ++cp) cps.push_back(cp);
    for (uint32_t cp : {0x0301u, 0x0302u, 0x0303u, 0x0323u, 0x1ea5u, 0x1ec5u, 0x1ec7u,
                       0x4e00u, 0x5b57u, 0x6f22u, 0xfffdu}) cps.push_back(cp);
    for (uint32_t cp : cps) {
      const uint8_t w = 6 + tier + variant, h = 9 + tier * 2;
      const uint32_t offset = bitmap.size();
      const uint16_t count = (w * h + 3) / 4;
      bitmap.resize(offset + count);
      for (int n = 0; n < w * h; ++n)
        bitmap[offset + n / 4] |= ((n + cp + variant) % 4) << (6 - (n % 4) * 2);
      intervals.push_back({cp, cp, static_cast<uint32_t>(glyphs.size())});
      glyphs.push_back({w, h, static_cast<uint16_t>((w + 1) * 16), static_cast<int16_t>(-variant),
                        static_cast<int16_t>(h + (cp < 0x400 && cp >= 0x300 ? 7 : 0)), count, offset});
    }
    data.bitmap = bitmap.data(); data.glyph = glyphs.data(); data.intervals = intervals.data();
    data.intervalCount = intervals.size(); data.advanceY = 18 + tier * 3;
    data.ascender = 14 + tier * 2; data.descender = -4; data.is2Bit = true;
  }
};
void drawCorpus(GfxRenderer& renderer, int y) {
  using Style = EpdFontFamily::Style;
  TextBlock one({"ấ", "漢字", "ễ", "ệ"}, {0, 70, 160, 210},
                {EpdFontFamily::REGULAR, EpdFontFamily::ITALIC, EpdFontFamily::BOLD, EpdFontFamily::SUB},
                {}, {}, BlockStyle{}, {"a", "reading", "", "last"});
  one.render(renderer, 42, 35, y);
  TextBlock group({"ấ", "ễ", "ệ", "tail"}, {0, 55, 120, 185},
    {static_cast<Style>(EpdFontFamily::UNDERLINE | EpdFontFamily::ITALIC),
     static_cast<Style>(EpdFontFamily::RUBY_CONTINUE | EpdFontFamily::UNDERLINE),
     static_cast<Style>(EpdFontFamily::RUBY_CONTINUE | EpdFontFamily::STRIKETHROUGH), EpdFontFamily::SUP},
    {}, {}, BlockStyle{}, {"a long annotation", "", "", "sup"});
  group.setLetterSpacing(2);
  group.render(renderer, 42, 30, y + 35);
  TextBlock cap({"Initial", "end"}, {0, 180}, {static_cast<Style>(EpdFontFamily::ITALIC | EpdFontFamily::DROP_CAP), EpdFontFamily::REGULAR},
                {}, {}, BlockStyle{}, {"drop", ""});
  cap.setDropCapHeight(38);
  cap.render(renderer, 42, 25, y + 80);
}
}
HalDisplay::HalDisplay() = default;
HalDisplay::~HalDisplay() = default;
void HalDisplay::cleanupGrayscaleBuffers(const uint8_t*) {}
uint8_t* HalDisplay::getFrameBuffer() const { return pixels.data(); }
uint16_t HalDisplay::getDisplayWidth() const { return width; }
uint16_t HalDisplay::getDisplayHeight() const { return height; }
uint16_t HalDisplay::getDisplayWidthBytes() const { return width / 8; }
uint32_t HalDisplay::getBufferSize() const { return width / 8 * height; }
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
