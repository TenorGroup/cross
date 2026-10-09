#pragma once

#include <algorithm>
#include <cstdint>

#include "UglyWobble.h"

namespace ugly::icons {

enum class Id : uint8_t {
  File, Folder, Book, Text, Image, Wifi, Bluetooth, BluetoothLost, Heart, Star,
  Left, Right, Up, Down, Tick, Back, Erase, Battery, Bolt, Usb, Library, Hotspot,
  Blocks, Bookmark, Gear, Stats, Clock, Sun, Moon, Refresh, Contents, Font, More,
  AlignLeft, AlignRight, AlignCenter, AlignJustify, AlignBook,
  Spacing1, Spacing2, Spacing3, Spacing4, Spacing5, DropOff, DropDefault, DropLarge, Count
};

struct Node { int8_t x, y; };
struct Point { int x, y; };
struct Rect { int x, y, width, height; };
struct Path { const Node* nodes; uint8_t count; };

inline constexpr Node FILE[] = {{68,12},{24,15},{21,86},{80,83},{79,32},{68,12},{65,35},{79,32}};
inline constexpr Node FOLDER[] = {{16,83},{14,29},{33,26},{39,14},{60,17},{66,31},{84,28},{87,81},{16,83},{23,42},{80,40}};
inline constexpr Node BOOK[] = {{51,84},{30,76},{13,79},{15,18},{34,16},{51,27},{52,85},{54,26},{74,17},{88,20},{86,79},{68,76},{51,84}};
inline constexpr Node TEXT[] = {{68,12},{24,15},{21,86},{80,83},{79,32},{68,12},{65,35},{79,32},{-1,0},{32,46},{64,44},{-1,0},{31,59},{67,61},{-1,0},{33,74},{59,73}};
inline constexpr Node IMAGE[] = {{17,83},{19,18},{85,17},{83,83},{17,83},{36,50},{54,69},{68,44},{83,83},{-1,0},{36,30},{30,34},{33,41},{41,38},{40,31},{36,30}};
inline constexpr Node WIFI[] = {{12,37},{30,20},{53,17},{76,22},{89,36},{-1,0},{24,53},{41,39},{59,38},{78,52},{-1,0},{38,68},{50,56},{64,67},{-1,0},{49,82},{52,80},{54,85},{49,85},{49,82}};
inline constexpr Node BT[] = {{28,29},{76,70},{48,90},{48,10},{75,30},{28,71}};
inline constexpr Node BT_LOST[] = {{23,29},{65,70},{40,90},{40,10},{65,30},{23,71},{-1,0},{74,44},{90,58},{-1,0},{89,43},{74,59}};
inline constexpr Node HEART[] = {{50,88},{18,59},{12,34},{24,15},{40,17},{50,33},{63,14},{83,17},{91,35},{83,60},{50,88}};
inline constexpr Node STAR[] = {{50,8},{77,90},{7,37},{93,37},{23,90},{50,8}};
inline constexpr Node LEFT[] = {{86,50},{17,53},{45,27},{17,53},{44,77}};
inline constexpr Node RIGHT[] = {{14,51},{84,48},{56,22},{84,48},{54,75}};
inline constexpr Node UP[] = {{49,86},{52,15},{25,44},{52,15},{77,44}};
inline constexpr Node DOWN[] = {{50,14},{47,84},{22,55},{47,84},{75,54}};
inline constexpr Node TICK[] = {{15,52},{37,77},{85,20}};
inline constexpr Node BACK[] = {{83,77},{84,48},{72,35},{17,37},{41,16},{17,37},{44,60}};
inline constexpr Node ERASE[] = {{13,50},{35,17},{86,24},{80,76},{34,83},{13,50},{-1,0},{42,38},{66,65},{-1,0},{67,38},{42,65}};
inline constexpr Node BATTERY[] = {{12,25},{80,23},{81,41},{93,42},{92,61},{82,60},{83,77},{12,79},{12,25}};
inline constexpr Node BOLT[] = {{64,12},{27,54},{53,51},{37,88},{78,39},{53,42},{64,12}};
inline constexpr Node USB[] = {{49,88},{50,14},{40,27},{50,14},{61,27},{-1,0},{51,63},{27,49},{27,32},{-1,0},{50,74},{74,53},{74,32},{65,32},{65,42},{83,42},{83,32},{74,32}};
inline constexpr Node LIBRARY[] = {{15,85},{17,20},{32,18},{34,83},{15,85},{-1,0},{39,83},{39,14},{56,17},{57,84},{39,83},{-1,0},{67,85},{60,29},{77,24},{88,81},{67,85}};
inline constexpr Node HOTSPOT[] = {{32,88},{51,30},{70,88},{32,88},{-1,0},{36,54},{66,57},{-1,0},{27,40},{18,30},{23,14},{-1,0},{75,13},{83,26},{76,40},{-1,0},{51,26},{46,21},{52,16},{56,23},{51,26}};
inline constexpr Node BLOCKS[] = {{15,42},{16,16},{41,17},{42,43},{15,42},{-1,0},{58,42},{59,15},{83,16},{85,41},{58,42},{-1,0},{17,84},{14,58},{42,58},{42,85},{17,84},{-1,0},{58,83},{60,57},{86,60},{82,86},{58,83}};
inline constexpr Node BOOKMARK[] = {{28,87},{28,15},{75,16},{74,86},{50,66},{28,87}};
inline constexpr Node GEAR[] = {{14,31},{53,32},{72,32},{77,24},{70,17},{60,20},{59,30},{67,39},{79,32},{87,33},{-1,0},{14,71},{30,73},{45,70},{46,62},{38,57},{29,61},{29,70},{37,79},{46,72},{87,71}};
inline constexpr Node STATS[] = {{13,86},{16,59},{32,55},{33,86},{48,85},{47,34},{62,33},{63,84},{76,86},{73,14},{86,18},{87,85},{13,86}};
inline constexpr Node CLOCK[] = {{52,13},{24,22},{12,49},{25,77},{52,88},{80,73},{89,46},{77,22},{52,13},{-1,0},{50,28},{50,51},{70,62}};
inline constexpr Node SUN[] = {{50,30},{30,40},{29,60},{50,71},{70,60},{72,41},{50,30},{50,10},{-1,0},{76,24},{86,15},{-1,0},{81,50},{94,50},{-1,0},{76,76},{87,86},{-1,0},{50,82},{50,94},{-1,0},{24,76},{14,85},{-1,0},{20,50},{7,50},{-1,0},{25,23},{15,14}};
inline constexpr Node MOON[] = {{72,14},{40,12},{15,35},{15,63},{37,85},{68,87},{86,68},{58,65},{42,45},{48,26},{72,14}};
inline constexpr Node REFRESH[] = {{77,66},{67,84},{39,88},{17,70},{14,41},{33,19},{63,16},{83,37},{63,36},{83,37},{85,16}};
inline constexpr Node CONTENTS[] = {{15,24},{24,21},{24,28},{15,24},{-1,0},{35,24},{86,23},{-1,0},{17,49},{25,47},{25,55},{17,49},{-1,0},{37,49},{82,51},{-1,0},{16,77},{24,74},{24,81},{16,77},{-1,0},{37,77},{85,75}};
inline constexpr Node FONT[] = {{13,84},{38,16},{62,85},{-1,0},{25,57},{51,56},{-1,0},{88,83},{87,50},{68,46},{63,65},{68,82},{86,70}};
inline constexpr Node MORE[] = {{20,45},{13,42},{11,52},{19,58},{24,51},{20,45},{-1,0},{50,45},{44,42},{42,52},{50,58},{55,51},{50,45},{-1,0},{80,45},{74,42},{72,52},{80,58},{85,51},{80,45}};
inline constexpr Node ALIGN_LEFT[] = {{16,22},{85,24},{-1,0},{16,41},{59,42},{-1,0},{16,60},{84,63},{-1,0},{16,80},{54,81}};
inline constexpr Node ALIGN_RIGHT[] = {{16,22},{85,24},{-1,0},{40,41},{85,42},{-1,0},{17,60},{85,63},{-1,0},{45,80},{85,81}};
inline constexpr Node ALIGN_CENTER[] = {{16,22},{85,24},{-1,0},{31,41},{69,42},{-1,0},{16,60},{84,63},{-1,0},{33,80},{66,81}};
inline constexpr Node ALIGN_JUSTIFY[] = {{16,22},{85,24},{-1,0},{16,41},{84,42},{-1,0},{16,60},{84,63},{-1,0},{16,80},{85,81}};
inline constexpr Node ALIGN_BOOK[] = {{20,84},{21,16},{78,17},{80,84},{20,84},{-1,0},{31,33},{68,34},{-1,0},{31,48},{57,49},{-1,0},{31,63},{68,65}};
inline constexpr Node SPACING1[] = {{30,41},{86,43},{-1,0},{30,58},{85,60},{-1,0},{15,70},{15,32},{8,41},{15,32},{22,41},{-1,0},{8,61},{15,70},{22,61}};
inline constexpr Node SPACING2[] = {{30,35},{86,37},{-1,0},{30,65},{85,67},{-1,0},{15,76},{15,26},{8,35},{15,26},{22,35},{-1,0},{8,67},{15,76},{22,67}};
inline constexpr Node SPACING3[] = {{30,29},{86,31},{-1,0},{30,71},{85,73},{-1,0},{15,82},{15,20},{8,29},{15,20},{22,29},{-1,0},{8,73},{15,82},{22,73}};
inline constexpr Node SPACING4[] = {{30,23},{86,25},{-1,0},{30,77},{85,79},{-1,0},{15,88},{15,14},{8,23},{15,14},{22,23},{-1,0},{8,79},{15,88},{22,79}};
inline constexpr Node SPACING5[] = {{30,17},{86,19},{-1,0},{30,83},{85,85},{-1,0},{15,94},{15,8},{8,17},{15,8},{22,17},{-1,0},{8,85},{15,94},{22,85}};
inline constexpr Node DROP_OFF[] = {{17,24},{84,26},{-1,0},{17,43},{63,45},{-1,0},{17,63},{82,62},{-1,0},{17,83},{68,81}};
inline constexpr Node DROP_DEFAULT[] = {{14,60},{30,20},{45,62},{-1,0},{20,44},{38,44},{-1,0},{55,28},{85,29},{-1,0},{55,46},{80,47},{-1,0},{17,78},{85,79}};
inline constexpr Node DROP_LARGE[] = {{13,83},{31,18},{51,84},{-1,0},{20,57},{44,57},{-1,0},{62,28},{86,29},{-1,0},{62,49},{85,50},{-1,0},{62,72},{86,74}};

template <unsigned Count>
constexpr Path pathOf(const Node (&nodes)[Count]) { return {nodes, Count}; }

inline constexpr Path PATHS[] = {
  pathOf(FILE),pathOf(FOLDER),pathOf(BOOK),pathOf(TEXT),pathOf(IMAGE),pathOf(WIFI),pathOf(BT),pathOf(BT_LOST),
  pathOf(HEART),pathOf(STAR),pathOf(LEFT),pathOf(RIGHT),pathOf(UP),pathOf(DOWN),pathOf(TICK),pathOf(BACK),pathOf(ERASE),
  pathOf(BATTERY),pathOf(BOLT),pathOf(USB),pathOf(LIBRARY),pathOf(HOTSPOT),pathOf(BLOCKS),pathOf(BOOKMARK),
  pathOf(GEAR),pathOf(STATS),pathOf(CLOCK),pathOf(SUN),pathOf(MOON),pathOf(REFRESH),
  pathOf(CONTENTS),pathOf(FONT),pathOf(MORE),pathOf(ALIGN_LEFT),pathOf(ALIGN_RIGHT),
  pathOf(ALIGN_CENTER),pathOf(ALIGN_JUSTIFY),pathOf(ALIGN_BOOK),pathOf(SPACING1),pathOf(SPACING2),
  pathOf(SPACING3),pathOf(SPACING4),pathOf(SPACING5),pathOf(DROP_OFF),pathOf(DROP_DEFAULT),pathOf(DROP_LARGE)
};
static_assert(sizeof(PATHS) / sizeof(PATHS[0]) == static_cast<unsigned>(Id::Count));

class FrameState {
 public:
  void enter(uint32_t seed) { seed_ = seed; valid_ = false; }
  bool needsSeed(uint64_t key) const { return valid_ && key != key_; }
  uint32_t bind(uint64_t key, uint32_t nextSeed) {
    if (valid_ && key != key_) seed_ = nextSeed;
    key_ = key;
    valid_ = true;
    return seed_;
  }
 private:
  uint64_t key_ = 0;
  uint32_t seed_ = 0;
  bool valid_ = false;
};

inline Point midpoint(Point first, Point second) {
  return {(first.x + second.x) / 2, (first.y + second.y) / 2};
}

struct Warp {
  int scaleX, scaleY, lean, bow, curl, shiftX, shiftY;
  int gain = 256;

  Point offset(Node node) const {
    const int horizontal = node.x - 50, vertical = node.y - 50;
    return {((horizontal * (90 + scaleX) * 256 / 100 - horizontal * 90 * 256 / 100) + vertical * lean * 256 / 100 +
             bow * (vertical * vertical - 625) * 256 / 2500) * gain / 256,
            ((vertical * (90 + scaleY) * 256 / 100 - vertical * 90 * 256 / 100) +
             curl * horizontal * vertical * 256 / 2500) * gain / 256};
  }
};

inline Warp rawWarp(Id id, uint32_t seed) {
  seed ^= static_cast<unsigned>(id) * 83492791u;
  return {logic::wobble(seed, 1, 10), logic::wobble(seed, 2, 10),
          logic::wobble(seed, 3, 16), logic::wobble(seed, 4, 12), logic::wobble(seed, 7, 9),
          logic::wobble(seed, 5, 5), logic::wobble(seed, 6, 5)};
}

inline uint64_t warpEnergy(Path path, const Warp& warp) {
  uint64_t total = 0;
  for (int index = 0; index < path.count; ++index) {
    if (path.nodes[index].x < 0) continue;
    const Point offset = warp.offset(path.nodes[index]);
    total += static_cast<int64_t>(offset.x) * offset.x + static_cast<int64_t>(offset.y) * offset.y;
  }
  return total;
}

inline Warp warpFor(Id id, uint32_t seed) {
  const Path path = PATHS[static_cast<unsigned>(id)];
  Warp warp = rawWarp(id, seed);
  const Warp reference = rawWarp(id, 7);
  if (seed == 7) return reference;
  auto nearReference = [](int value, int approved) {
    const int magnitude = approved < 0 ? -approved : approved;
    const int minimum = (magnitude * 7 + 9) / 10;
    if (value >= minimum || value <= -minimum) return value;
    return (value == 0 ? approved : value) < 0 ? -minimum : minimum;
  };
  warp.lean = nearReference(warp.lean, reference.lean);
  warp.bow = nearReference(warp.bow, reference.bow);
  warp.curl = nearReference(warp.curl, reference.curl);
  uint64_t energy = warpEnergy(path, warp);
  if (energy == 0) {
    warp.scaleX = reference.scaleX;
    warp.scaleY = reference.scaleY;
    warp.lean = reference.lean;
    warp.bow = reference.bow;
    warp.curl = reference.curl;
    energy = warpEnergy(path, warp);
  }
  const uint64_t target = warpEnergy(path, reference) * 256 * 256;
  int lower = 0, upper = 65536;
  while (upper - lower > 1) {
    const int middle = (lower + upper) / 2;
    if (energy * middle * middle <= target) lower = middle;
    else upper = middle;
  }
  warp.gain = lower;
  return warp;
}

template <typename Emit>
void walk(Id id, Rect box, uint32_t seed, Emit emit) {
  if (box.width < 4 || box.height < 4 || id >= Id::Count) return;
  const Path path = PATHS[static_cast<unsigned>(id)];
  const Warp warp = warpFor(id, seed);
  auto point = [&](Node node) {
    const int horizontal = node.x - 50, vertical = node.y - 50;
    const Point offset = warp.offset(node);
    const int nx = 50 * 256 + horizontal * 90 * 256 / 100 + offset.x + warp.shiftX * 256;
    const int ny = 50 * 256 + vertical * 90 * 256 / 100 + offset.y + warp.shiftY * 256;
    return Point{(box.x + 2) * 256 + std::clamp(nx, 0, 100 * 256) * (box.width - 5) / 100,
                 (box.y + 2) * 256 + std::clamp(ny, 0, 100 * 256) * (box.height - 5) / 100};
  };
  int start = 0;
  while (start < path.count) {
    int end = start;
    while (end < path.count && path.nodes[end].x >= 0) ++end;
    if (end - start >= 2) {
      Point previous = point(path.nodes[start]);
      for (int index = start; index < end; ++index) {
        const Point control = point(path.nodes[index]);
        const Point before = index == start ? control : point(path.nodes[index - 1]);
        const Point after = index + 1 == end ? control : point(path.nodes[index + 1]);
        const Point from{(control.x * 3 + before.x) / 4, (control.y * 3 + before.y) / 4};
        const Point to{(control.x * 3 + after.x) / 4, (control.y * 3 + after.y) / 4};
        if (from.x != previous.x || from.y != previous.y) emit(previous, from);
        previous = from;
        for (int step = 1; step <= 5; ++step) {
          const int left = 5 - step;
          const Point next{(left * left * from.x + 2 * left * step * control.x + step * step * to.x) / 25,
                           (left * left * from.y + 2 * left * step * control.y + step * step * to.y) / 25};
          if (next.x != previous.x || next.y != previous.y) emit(previous, next);
          previous = next;
        }
      }
    }
    start = end + 1;
  }
}

}
