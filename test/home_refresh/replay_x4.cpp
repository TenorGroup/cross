// Replay simulator frames through the real display facade and driver, recording each DRF.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
#define private public
#include "FreeInkDisplay.h"
#include "src/driver/Uc8279X4Driver.h"
#undef private
using freeink::FreeInkDisplay;

struct Rect { unsigned x, y, w, h; };

int main(int argc, char** argv) {
  assert(argc == 3);
  std::map<unsigned, Rect> windows;
  FILE* mapping = fopen(argv[2], "r");
  assert(mapping);
  unsigned index;
  Rect r{};
  while (fscanf(mapping, "%u %u %u %u %u", &index, &r.x, &r.y, &r.w, &r.h) == 5) windows[index] = r;
  fclose(mapping);
  FILE* input = fopen(argv[1], "rb");
  assert(input);
  freeink::Uc8279X4Driver driver;
  FreeInkDisplay panel(1, 2, 3, 4, 5, 6);
  panel._driver = &driver;
  bool partial = false;
  unsigned psr = 0, speed = 0, scan = 1;
  Rect window{0, 0, 800, 480};
  unsigned char header[12];
  index = 0;
  while (fread(header, 1, sizeof(header), input) == sizeof(header)) {
    assert(header[0] == 'T');
    const unsigned op = header[1], a = header[2], b = header[3];
    const unsigned c = header[4] | header[5] << 8, d = header[6] | header[7] << 8;
    const unsigned len = header[8] | header[9] << 8 | header[10] << 16 | unsigned(header[11]) << 24;
    std::vector<uint8_t> data(len);
    assert(fread(data.data(), 1, len, input) == len);
    panel._bus.clear();
    if (op == 1) {
      panel.begin();
      if (a) panel.skipInitialResync();
      else panel.requestResync();
    }
    if (len == 48000) memcpy(panel.getFrameBuffer(), data.data(), len);
    switch (op) {
      case 2:
        if (windows.count(index)) {
          const auto box = windows.at(index);
          panel.displayWindow(box.x, box.y, box.w, box.h);
        } else panel.displayBuffer(static_cast<FreeInkDisplay::RefreshMode>(a), b);
        break;
      case 3: panel.displayGrayscaleBase(static_cast<FreeInkDisplay::RefreshMode>(a), b); break;
      case 4:
        assert(panel.displayGrayscaleBase(static_cast<freeink::GrayscaleMode>(a),
                                          static_cast<FreeInkDisplay::RefreshMode>(b), c));
        break;
      case 5: panel.copyGrayscaleLsbBuffers(data.data()); break;
      case 6: panel.copyGrayscaleMsbBuffers(data.data()); break;
      case 7:
        panel.writeGrayscalePlaneStrip(a ? FreeInkDisplay::GRAY_PLANE_LSB : FreeInkDisplay::GRAY_PLANE_MSB,
                                       data.data(), c, d);
        break;
      case 8: assert(!b); panel.displayGrayBuffer(a, nullptr, c); break;
      case 9: panel.cleanupGrayscaleBuffers(data.empty() ? nullptr : data.data()); break;
      case 10:
        if (a) panel.preconditionGrayscale();
        else panel.preconditionGrayscale(c, data[0] | data[1] << 8, d, data[2] | data[3] << 8);
        break;
      case 11: panel.deepSleep(); break;
      case 12: panel.setInverted(a); break;
    }
    size_t ram = 0;
    for (const auto& write : panel._bus.writes)
      if (write.command == 0x10 || write.command == 0x13) ram += write.bytes.size();
    for (const auto& write : panel._bus.writes) {
      const auto& bytes = write.bytes;
      if (write.command == 0x91) partial = true;
      if (write.command == 0x92) partial = false;
      if (write.command == 0x00 && !bytes.empty()) psr = bytes[0];
      if (write.command == 0xE5 && !bytes.empty()) speed = bytes[0];
      if (write.command == 0x90 && bytes.size() == 9) {
        const unsigned x = bytes[0] << 8 | bytes[1], xe = bytes[2] << 8 | bytes[3];
        const unsigned y = bytes[4] << 8 | bytes[5], ye = bytes[6] << 8 | bytes[7];
        window = {x, y - 120, xe - x + 1, ye - y + 1};
        scan = bytes[8];
      }
      if (write.command != 0x12) continue;
      const auto box = partial ? window : Rect{0, 0, 800, 480};
      const char* mode = psr & 0x20 ? "gray" : speed == 0x5A ? "fast" : "full";
      printf("{\"record\":%u,\"op\":%u,\"mode\":\"%s\",\"rect\":[%u,%u,%u,%u],"
             "\"pt_scan\":%u,\"ram_bytes\":%zu}\n", index, op, mode, box.x, box.y, box.w, box.h, scan, ram);
    }
    ++index;
  }
  fclose(input);
  panel.releaseBuffers();
}
