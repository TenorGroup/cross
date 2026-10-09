#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace tenorchrome {
enum class X3PathIcon : uint8_t {
  Recent, File, Favorites, Stats, Settings, Book, Display, Reading, Controls, System,
  Device, Keyboard, More, Sleep, Motion, Status, Language, About, Position, Network
};

struct X3PathNode {
  X3PathIcon icon;
  std::string title;
};

struct X3PathPill {
  X3PathIcon icon;
  std::string text;
  int width;
  bool current;
};

constexpr int X3_PATH_GAP = 8;
constexpr int X3_PATH_PADDING = 14;

template <class Renderer>
std::vector<X3PathPill> layoutX3Navigation(const Renderer& renderer, const int font,
                                          const std::vector<X3PathNode>& nodes,
                                          const int available, const int height) {
  if (nodes.size() < 2 || available <= 0 || height <= 0) return {};
  const int fullTitle = renderer.getTextWidth(font, nodes.back().title.c_str()) + 2 * X3_PATH_PADDING;
  std::vector<X3PathPill> pills;
  const bool collapse = nodes.size() > 3 &&
      static_cast<int>(nodes.size() - 1) * (height + X3_PATH_GAP) + fullTitle > available;
  pills.push_back({nodes.front().icon, "", height, false});
  if (collapse) {
    pills.push_back({X3PathIcon::More, "…", height, false});
    pills.push_back({nodes[nodes.size() - 2].icon, "", height, false});
  } else {
    for (size_t index = 1; index + 1 < nodes.size(); ++index)
      pills.push_back({nodes[index].icon, "", height, false});
  }
  const int room = std::max(0, available - static_cast<int>(pills.size()) * (height + X3_PATH_GAP));
  std::string title = nodes.back().title;
  if (fullTitle > room) {
    const std::string ellipsis = "…";
    const int textRoom = std::max(0, room - 2 * X3_PATH_PADDING);
    std::string prefix;
    for (size_t index = 0; index < title.size();) {
      size_t next = index + 1;
      while (next < title.size() && (static_cast<unsigned char>(title[next]) & 0xc0) == 0x80) ++next;
      const auto candidate = title.substr(0, next) + ellipsis;
      if (renderer.getTextWidth(font, candidate.c_str()) > textRoom) break;
      prefix = title.substr(0, next);
      index = next;
    }
    title = renderer.getTextWidth(font, ellipsis.c_str()) <= textRoom ? prefix + ellipsis : "";
  }
  const int width = std::min(room, renderer.getTextWidth(font, title.c_str()) + 2 * X3_PATH_PADDING);
  pills.push_back({nodes.back().icon, std::move(title), width, true});
  return pills;
}
}
