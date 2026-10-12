#include "RasterFixture.h"
#include <Epub/Page.h>
#include <SdCardFont.h>
#include <cassert>
#include <chrono>
#include "../huge_book/HeapCap.h"

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  std::filesystem::create_directories(argv[1]);
  std::ifstream sdFile(STRIP_SD_FONT_PATH, std::ios::binary);
  Storage.files["/raster.cpfont"] = std::vector<uint8_t>(std::istreambuf_iterator<char>(sdFile), {});
  if (Storage.files["/raster.cpfont"].empty()) return 4;
  size_t frames = 0, changed = 0, heapPeak = 0;
  unsigned long bwCalls = 0, bwAdvance = 0;
  const auto started = std::chrono::steady_clock::now();
  unsigned long beforeCalls = 0, afterCalls = 0, beforeAdvance = 0, afterAdvance = 0;
  for (int device = 0; device < 2; ++device) {
    width = device ? 800 : 792; height = device ? 480 : 528;
    pixels.resize(width / 8 * height);
    for (int fontSource = 0; fontSource < 3; ++fontSource) {
    for (int tier = 0; tier < 3; ++tier) {
      Face regular(tier, 0), bold(tier, 1), italic(tier, 2), boldItalic(tier, 3);
      Face fallback(tier + 1, 1);
      // Only the fallback covers CJK; its larger ascender exercises centering.
      for (auto* face : {&regular, &bold, &italic, &boldItalic}) {
        auto& ranges = face->intervals;
        ranges.erase(std::remove_if(ranges.begin(), ranges.end(), [](const auto& r) {
          return r.first >= 0x4e00 && r.first < 0xf000;
        }), ranges.end());
        face->data.intervals = ranges.data(); face->data.intervalCount = ranges.size();
      }
      SdCardFont sdFont;
      HalDisplay display; GfxRenderer renderer(display); renderer.begin();
      if (!fontSource) renderer.insertFont(42, EpdFontFamily(&regular.font, &bold.font, &italic.font, &boldItalic.font));
      renderer.insertFont(43, EpdFontFamily(&fallback.font));
      renderer.setFallbackFont(42, 43);
      if (fontSource) {
        if (!sdFont.load("/raster.cpfont")) return 5;
        renderer.insertFont(42, EpdFontFamily(sdFont.getEpdFont(0), sdFont.getEpdFont(1),
                                             sdFont.getEpdFont(2), sdFont.getEpdFont(3)));
        renderer.registerSdCardFont(42, &sdFont);
        if (fontSource == 1)
          sdFont.prewarm("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ ấễệấễệ😀", 0x0f);
      }
      for (int rotation = 0; rotation < 4; ++rotation) {
        renderer.setOrientation(static_cast<GfxRenderer::Orientation>(rotation));
        for (int boundary : {0, 80, 160}) {
          if (fontSource == 0) heapcap::reset(SIZE_MAX);
          if (fontSource == 2) sdFont.releaseResidentCaches();
          std::vector<std::unique_ptr<PageLine>> lines;
          using S = EpdFontFamily::Style;
          auto add = [&](std::vector<std::string> words, std::vector<int16_t> xs, std::vector<S> styles,
                         std::vector<std::string> ruby, int x, int y, int spacing = 0, int cap = 0) {
            auto block = std::make_unique<TextBlock>(words, xs, styles, std::vector<uint8_t>{},
                                                   std::vector<uint16_t>{}, BlockStyle{}, ruby);
            block->setLetterSpacing(spacing); block->setDropCapHeight(cap);
            lines.push_back(std::make_unique<PageLine>(std::move(block), x, y));
          };
          for (int i = 0; i < 6; ++i) {
            const int x = (i % 3) * 80, y = boundary + i * 44;
            add({"ấ", "ễ", "ệ"}, {0, 24, 48}, {S::ITALIC, S::REGULAR, S::BOLD}, {}, x, y);
            add({"ấ", "ễ", "ệ"}, {0, 25, 50},
                {static_cast<S>(S::ITALIC | S::UNDERLINE), S::SUP, static_cast<S>(S::SUB | S::STRIKETHROUGH)},
                {}, x + 80, y + 13, i % 2 ? -2 : 3);
          }
          add({"漢字", "a", "b"}, {0, 26, 44}, {S::ITALIC, S::REGULAR, S::REGULAR},
              {"reading", "a long annotation", ""}, 79, boundary - 12);
          add({"a", "b", "c"}, {0, 24, 48}, {S::REGULAR, S::RUBY_CONTINUE, S::RUBY_CONTINUE},
              {"long group ruby", "", ""}, 159, boundary + 67);
          add({"Initial", "end"}, {0, 94}, {static_cast<S>(S::DROP_CAP | S::ITALIC | S::UNDERLINE), S::REGULAR},
              {"drop", ""}, 0, boundary + 159, 2, 58);
          add({"missing 😀"}, {0}, {S::REGULAR}, {}, 159, boundary + 38);
          add({"jiffy"}, {0}, {static_cast<S>(S::ITALIC | S::UNDERLINE)}, {}, 80, boundary - 1, -2);
          auto focus = std::make_unique<TextBlock>(std::vector<std::string>{"focused"}, std::vector<int16_t>{0},
              std::vector<S>{S::ITALIC}, std::vector<uint8_t>{3}, std::vector<uint16_t>{41});
          lines.push_back(std::make_unique<PageLine>(std::move(focus), 78, boundary + 78));
          auto draw = [&](bool culled) {
            for (auto& line : lines) {
              if (culled) line->render(renderer, 42, 0, 0);
              else line->getBlock()->render(renderer, 42, line->xPos, line->yPos);
            }
            static const uint8_t image[32] = {0x55,0xaa,0x00,0xff,0xaa,0x55,0xff,0x00};
            renderer.drawBits(image, 2, 0, 0, 16, 16, 250, boundary + 81);
          };
          // The normal page pass prepares bounds before either gray plane.
          renderer.setRenderMode(GfxRenderer::BW);
          rasterBlockCalls = rasterAdvanceCalls = 0;
          std::fill(pixels.begin(), pixels.end(), 0xff); draw(true);
          bwCalls += rasterBlockCalls; bwAdvance += rasterAdvanceCalls;
          if (fontSource == 0) heapPeak = std::max(heapPeak, heapcap::peak);
          heapcap::stop();
          for (int mode = 1; mode <= 2; ++mode) {
            renderer.setRenderMode(static_cast<GfxRenderer::RenderMode>(mode));
            std::vector<uint8_t> expected(pixels.size()), actual(pixels.size());
            auto tiled = [&](bool culled, std::vector<uint8_t>& out) {
              for (unsigned y = 0; y < height; y += 80) {
                const unsigned rows = std::min(80u, height - y);
                auto* target = out.data() + y * width / 8;
                std::fill(target, target + rows * width / 8, 0x00);
                renderer.beginStripTarget(target, y, rows); draw(culled); renderer.endStripTarget();
              }
            };
            rasterBlockCalls = rasterAdvanceCalls = 0; tiled(false, expected);
            beforeCalls += rasterBlockCalls; beforeAdvance += rasterAdvanceCalls;
            rasterBlockCalls = rasterAdvanceCalls = 0; tiled(true, actual);
            afterCalls += rasterBlockCalls; afterAdvance += rasterAdvanceCalls;
            if (std::all_of(expected.begin(), expected.end(), [](auto byte) { return byte == 0; })) return 3;
            size_t difference = 0;
            for (size_t i = 0; i < expected.size(); ++i) difference += expected[i] != actual[i];
            changed += difference;
            const auto name = std::string(argv[1]) + "/d" + std::to_string(device) + "-t" + std::to_string(tier) +
                "-f" + std::to_string(fontSource) + "-r" + std::to_string(rotation) + "-y" + std::to_string(boundary) + "-m" + std::to_string(mode);
            for (const auto& entry : {std::pair<const char*,const std::vector<uint8_t>*>("before", &expected), {"after", &actual}}) {
              std::ofstream file(name + "-" + entry.first + ".bin", std::ios::binary);
              file.write(reinterpret_cast<const char*>(entry.second->data()), entry.second->size());
            }
            if (difference) std::printf("STRIP_DIFF %s bytes=%zu\n", name.c_str(), difference);
            ++frames;
          }
        }
      }
    }
  }
    }
  std::printf("STRIP_RASTER frames=%zu changed_bytes=%zu render_before=%lu render_after=%lu advance_before=%lu advance_after=%lu\n",
              frames, changed, beforeCalls, afterCalls, beforeAdvance, afterAdvance);
  std::printf("STRIP_COST bw_render=%lu bw_advance=%lu page_heap_peak=%zu block_size=%zu host_us=%lld\n",
              bwCalls, bwAdvance, heapPeak, sizeof(TextBlock),
              static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::steady_clock::now() - started).count()));
  return changed == 0 && afterCalls < beforeCalls && afterAdvance < beforeAdvance ? 0 : 1;
}
