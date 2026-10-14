#include <cstdlib>
#include <iostream>

#include "activities/settings/FontDownloadProgress.h"

namespace {

struct Box {
  const char* name;
  int top, height;
};

// A bar writes its percent label at bar + barHeight + labelGap, one line tall.
bool overlap(const Box& a, const Box& b) { return a.top < b.top + b.height && b.top < a.top + a.height; }

}  // namespace

int main() {
  constexpr int pageHeight = 800;
  constexpr int labelGap = 15;
  int bad = 0;
  for (int lineHeight = 16; lineHeight <= 48; lineHeight += 2) {
    for (int spacing = 4; spacing <= 16; spacing += 4) {
      for (int barHeight = 12; barHeight <= 24; barHeight += 4) {
        const int centerY = (pageHeight - lineHeight) / 2;
        const auto rows = fontdownload::transferLayout(centerY, lineHeight, spacing, barHeight, labelGap);
        const Box boxes[] = {{"name", rows.name, lineHeight},
                             {"status", rows.status, lineHeight},
                             {"bar1", rows.bar1, barHeight},
                             {"label1", rows.bar1 + barHeight + labelGap, lineHeight},
                             {"total", rows.total, lineHeight},
                             {"bar2", rows.bar2, barHeight},
                             {"label2", rows.bar2 + barHeight + labelGap, lineHeight},
                             {"size", rows.size, lineHeight}};
        for (int i = 0; i < 8; ++i) {
          for (int j = i + 1; j < 8; ++j) {
            if (!overlap(boxes[i], boxes[j])) continue;
            if (bad++ < 6)
              std::cerr << "overlap " << boxes[i].name << " " << boxes[j].name << " line=" << lineHeight
                        << " spacing=" << spacing << " bar=" << barHeight << '\n';
          }
        }
        if (rows.name < 0 || rows.size + lineHeight > pageHeight - 100) {
          std::cerr << "off screen line=" << lineHeight << " spacing=" << spacing << " bar=" << barHeight << '\n';
          ++bad;
        }
      }
    }
  }
  if (bad) {
    std::cerr << "FAIL: " << bad << " overlapping or off-screen cases\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
