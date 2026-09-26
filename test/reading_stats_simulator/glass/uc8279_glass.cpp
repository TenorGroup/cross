// Replays a simulator panel trace (scripts/patch_simulator_panel_trace.py) through the SDK's
// UC8279 driver, unchanged, over a model of the controller and the glass.
//
// The model, and what it takes as given:
//   - DTM1 (0x10) and DTM2 (0x13) are two 1-bit planes in the controller's RAM.
//   - A B/W refresh (the GC, DU and XTF_PRE_BW_MID banks) drives a pixel only where DTM1 and
//     DTM2 differ, to the DTM2 color. A pixel whose two bits agree keeps whatever the glass
//     holds, gray included (Uc8279Driver.cpp, displayStart; the skill's X3 UC8279 note).
//   - The XTH4 bank (absolute gray) drives every pixel of the window to the level its two bits
//     name: LSB (DTM1) white on levels 1 and 3, MSB (DTM2) white on 2 and 3.
//   - The XTF_AA bank (overlay gray) lightens a black pixel whose LSB or MSB mask is set: dark
//     with the LSB, light with the MSB alone, as the simulator preview draws it. A rough stand-in;
//     no wake path in the tests ends on it.
//   - Deep sleep (DSLP) loses both planes: the next reset leaves them holding noise. A reset
//     without DSLP before it (a restart while awake) keeps them.
// Every RAM write the driver makes outside the full-panel window, and every refresh outside the
// partial window, is counted as an anomaly instead of being modelled.
//
// Usage: uc8279_glass <trace> [--dump <op>:<file.pgm>]...
// One JSON line per trace record on stdout.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "bus/EpdBus.h"
#include "driver/Uc8279Driver.h"
#include "lut/Uc8279X3Luts.h"

namespace {
constexpr int W = 792;
constexpr int H = 528;
constexpr int WB = W / 8;
constexpr size_t SIZE = static_cast<size_t>(WB) * H;
enum Level : uint8_t { BLACK = 0, DARK = 1, LIGHT = 2, WHITE = 3, UNKNOWN = 4 };
enum class Bank { None, Gc, Du, PreBwMid, XtfAa, Xth4 };
const char* bankName(Bank b) {
  switch (b) {
    case Bank::Gc:
      return "gc";
    case Bank::Du:
      return "du";
    case Bank::PreBwMid:
      return "pre";
    case Bank::XtfAa:
      return "aa";
    case Bank::Xth4:
      return "xth4";
    default:
      return "none";
  }
}

bool bit(const std::vector<uint8_t>& plane, int x, int y) { return plane[y * WB + x / 8] & (0x80 >> (x & 7)); }

struct Controller {
  std::vector<uint8_t> dtm1 = std::vector<uint8_t>(SIZE), dtm2 = std::vector<uint8_t>(SIZE);
  std::vector<uint8_t> glass = std::vector<uint8_t>(static_cast<size_t>(W) * H, WHITE);
  bool ramLost = true;  // the first process of a trace meets a controller in an unknown state
  uint32_t noise = 0x9E3779B9u;
  uint8_t command = 0;
  std::vector<uint8_t> param;
  bool ptin = false;
  int x0 = 0, x1 = W - 1, g0 = 0, g1 = H - 1;  // PTL window, x in pixels, y in gate rows
  Bank bank = Bank::None;
  int stripRow = -1;
  // Per record
  unsigned refreshes = 0;
  uint64_t driven = 0;
  Bank lastBank = Bank::None;
  unsigned anomalies = 0;
  std::string anomaly;

  bool fullWindow() const { return x0 == 0 && x1 == W - 1 && g0 == 0 && g1 == H - 1; }
  void flag(const char* what) {
    anomalies++;
    if (anomaly.empty()) anomaly = what;
  }
  std::vector<uint8_t>* plane(uint8_t c) { return c == 0x10 ? &dtm1 : c == 0x13 ? &dtm2 : nullptr; }
  void fillNoise(std::vector<uint8_t>& p) {
    for (auto& b : p) {
      noise ^= noise << 13;
      noise ^= noise >> 17;
      noise ^= noise << 5;
      b = static_cast<uint8_t>(noise);
    }
  }
  void reset() {
    if (ramLost) {
      fillNoise(dtm1);
      fillNoise(dtm2);
      ramLost = false;
    }
    ptin = false;
    x0 = 0, x1 = W - 1, g0 = 0, g1 = H - 1;
    bank = Bank::None;
  }
  void endParam() {
    if (command == 0x90 && param.size() == 9) {
      x0 = param[0] << 8 | param[1];
      x1 = param[2] << 8 | param[3];
      g0 = param[4] << 8 | param[5];
      g1 = param[6] << 8 | param[7];
    }
  }
  void identify(const uint8_t* p, size_t n) {
    if (command != 0x20) return;
    if (n == 42) {
      bank = !memcmp(p, &freeink::kUc8279X3_BwGc[0][1], 42)          ? Bank::Gc
             : !memcmp(p, &freeink::kUc8279X3_BwDu[0][1], 42)        ? Bank::Du
             : !memcmp(p, &freeink::kUc8279X3_XtfPreBwMid[0][1], 42) ? Bank::PreBwMid
                                                                     : Bank::None;
    } else if (n == 49) {
      bank = !memcmp(p, freeink::kUc8279X3_Xth4[0], 49)    ? Bank::Xth4
             : !memcmp(p, freeink::kUc8279X3_XtfAa[0], 49) ? Bank::XtfAa
                                                           : Bank::None;
    }
  }
  void cmd(uint8_t c) {
    endParam();
    command = c;
    param.clear();
    if (c == 0x91) ptin = true;
    if (c == 0x92) ptin = false;
    if (c == 0x07) ramLost = true;
    if (c == 0x10 || c == 0x13) stripRow = H - 1 - g0;
    if (c == 0x12) refresh();
  }
  void data(const uint8_t* p, size_t n) {
    param.insert(param.end(), p, p + n);
    identify(p, n);
    if (command == 0x90 && param.size() == 9) endParam();
  }
  void writePlane(uint8_t c, const uint8_t* src, uint8_t fill, bool invert) {
    auto* dst = plane(c);
    if (!dst) return;
    if (!ptin || !fullWindow()) flag("plane write outside the full window");
    for (size_t i = 0; i < SIZE; i++) (*dst)[i] = src ? static_cast<uint8_t>(invert ? ~src[i] : src[i]) : fill;
  }
  void writeRow(const uint8_t* p, size_t n) {
    auto* dst = plane(command);
    if (!dst || !ptin || n != WB || x0 != 0 || x1 != W - 1 || stripRow < H - 1 - g1 || stripRow > H - 1 - g0) {
      flag("strip write outside its window");
      return;
    }
    memcpy(dst->data() + static_cast<size_t>(stripRow) * WB, p, WB);
    stripRow--;
  }
  void refresh() {
    refreshes++;
    lastBank = bank;
    if (!ptin) flag("refresh outside the partial window");
    const int yTop = H - 1 - g1, yBottom = H - 1 - g0;
    for (int y = yTop; y <= yBottom; y++) {
      for (int x = x0; x <= x1; x++) {
        const bool o = bit(dtm1, x, y), n = bit(dtm2, x, y);
        uint8_t& g = glass[static_cast<size_t>(y) * W + x];
        switch (bank) {
          case Bank::Gc:
          case Bank::Du:
          case Bank::PreBwMid:
            if (o != n) {
              g = n ? WHITE : BLACK;
              driven++;
            }
            break;
          case Bank::Xth4:
            g = static_cast<uint8_t>((o ? 1 : 0) | (n ? 2 : 0));
            driven++;
            break;
          case Bank::XtfAa:
            if (g == BLACK && (o || n)) {
              g = o ? DARK : LIGHT;
              driven++;
            }
            break;
          case Bank::None:
            g = UNKNOWN;
            break;
        }
      }
    }
    if (bank == Bank::None) flag("refresh with no known waveform bank");
  }
};

Controller ctl;
}  // namespace

// The SDK's EpdBus, its bytes taken by the model instead of an SPI bus.
namespace freeink {
void EpdBus::begin(const EpdPins& pins, uint32_t, BusyPolarity busy, int8_t, int8_t) {
  _pins = pins;
  _busy = busy;
}
void EpdBus::reset(uint16_t) { ctl.reset(); }
void EpdBus::cmd(uint8_t c) { ctl.cmd(c); }
void EpdBus::data(uint8_t d) { ctl.data(&d, 1); }
void EpdBus::data(const uint8_t* d, uint16_t len) { ctl.data(d, len); }
void EpdBus::cmdData(uint8_t c, const uint8_t* d, uint16_t len) {
  ctl.cmd(c);
  ctl.data(d, len);
}
void EpdBus::cmdData2(uint8_t c, uint8_t d0, uint8_t d1) {
  const uint8_t d[2] = {d0, d1};
  cmdData(c, d, 2);
}
void EpdBus::beginTxn() {}
void EpdBus::endTxn() {}
void EpdBus::rawCmd(uint8_t c) { ctl.cmd(c); }
void EpdBus::rawData(uint8_t d) { ctl.data(&d, 1); }
void EpdBus::rawWriteBytes(const uint8_t* d, uint16_t len) { ctl.writeRow(d, len); }
void EpdBus::waitBusy(const char*) {}
void EpdBus::waitBusy(BusyPolarity, const char*) {}
void EpdBus::waitRefreshComplete(const char*) {}
void EpdBus::sendPlaneFlipped(uint8_t c, const uint8_t* p, uint16_t, uint16_t) {
  ctl.cmd(c);
  ctl.writePlane(c, p, 0, false);
}
void EpdBus::sendPlaneFlippedInverted(uint8_t c, const uint8_t* p, uint16_t, uint16_t) {
  ctl.cmd(c);
  ctl.writePlane(c, p, 0, true);
}
void EpdBus::fillPlane(uint8_t c, uint8_t fill, uint16_t, uint16_t) {
  ctl.cmd(c);
  ctl.writePlane(c, nullptr, fill, false);
}
}  // namespace freeink

namespace {
using freeink::GrayscaleMode;
using freeink::RefreshMode;

// lib/hal/HalDisplay.cpp over FreeInkDisplay.cpp, single framebuffer, X3, as far as the UC8279
// driver sees it. One Facade per process: a wake or a restart starts a fresh one.
struct Facade {
  std::unique_ptr<freeink::Uc8279Driver> drv;
  freeink::EpdBus bus;
  bool inverted = false, inversionDirty = false, grayFailed = false;
  GrayscaleMode grayMode = GrayscaleMode::Overlay;
  uint16_t grayRows[2] = {0, 0};

  static RefreshMode internal(uint8_t halMode) {
    return halMode == 0 ? RefreshMode::Full : halMode == 1 ? RefreshMode::Half : RefreshMode::Fast;
  }
  void cancelGrayscalePass() {
    if (grayMode != GrayscaleMode::Absolute) return;
    drv->requestResync(1);
    grayMode = GrayscaleMode::Overlay;
    grayRows[0] = grayRows[1] = 0;
    grayFailed = true;
  }
  bool accept(unsigned plane, const uint8_t* data, uint16_t y, uint16_t rows) {
    if (grayMode != GrayscaleMode::Absolute) return true;
    if (grayFailed || !data || !rows || (plane == 1 && grayRows[0] == 0) || y != grayRows[plane] || y >= H ||
        rows > H - y) {
      grayFailed = true;
      return false;
    }
    grayRows[plane] += rows;
    return true;
  }
  void begin(bool seamless) {
    drv = std::make_unique<freeink::Uc8279Driver>();
    inverted = inversionDirty = grayFailed = false;
    grayMode = GrayscaleMode::Overlay;
    bus.begin({0, 0, 0, 0, 0, 0}, 0, drv->busyPolarity());
    drv->begin(bus);
    // HalDisplay::begin: a splashless start trusts the panel; any other start from the power
    // button, a flash or a restart asks for a resync (X3 never reaches here after USB power).
    if (seamless)
      drv->skipInitialResync();
    else
      drv->requestResync(0);
  }
  void facadeDisplay(uint8_t mode, bool off, std::vector<uint8_t>& fb) {
    cancelGrayscalePass();
    grayFailed = false;
    if (inversionDirty && mode == 2) mode = 1;
    if (inverted)
      for (auto& b : fb) b = ~b;
    drv->display(bus, fb.data(), nullptr, internal(mode), off);
    if (inverted)
      for (auto& b : fb) b = ~b;
    inversionDirty = false;
  }
  void display(uint8_t mode, bool off, std::vector<uint8_t>& fb) {
    if (mode == 1) drv->requestResync(1);
    facadeDisplay(mode, off, fb);
  }
  void grayBase(uint8_t fallback, bool off, std::vector<uint8_t>& fb) {
    if (fallback == 1) drv->requestResync(1);
    cancelGrayscalePass();
    grayFailed = false;
    if (inverted || inversionDirty) {
      facadeDisplay(fallback, off, fb);
      return;
    }
    drv->beginGrayscale(bus, fb.data(), GrayscaleMode::Overlay, internal(fallback), off);
  }
  void grayBaseMode(uint8_t mode, uint8_t fallback, bool off, std::vector<uint8_t>& fb) {
    if (fallback == 1) drv->requestResync(0);
    cancelGrayscalePass();
    const auto m = mode ? GrayscaleMode::Absolute : GrayscaleMode::Overlay;
    const auto caps = drv->grayscaleCapabilities(m);
    if (inverted || !caps.supported()) return;
    if (inversionDirty && (m != GrayscaleMode::Absolute || caps.base == freeink::GrayscaleBase::Separate))
      facadeDisplay(fallback, off, fb);
    grayFailed = false;
    drv->beginGrayscale(bus, fb.data(), m, internal(fallback), off);
    grayMode = m;
    grayRows[0] = grayRows[1] = 0;
  }
  void copy(unsigned plane, const std::vector<uint8_t>& p) {
    if (inverted) return;
    const uint8_t* data = p.size() == SIZE ? p.data() : nullptr;
    if (!accept(plane, data, 0, H)) return;
    if (plane == 0)
      drv->copyGrayscaleLsb(bus, data);
    else
      drv->copyGrayscaleMsb(bus, data);
  }
  void strip(bool lsb, uint16_t y, uint16_t rows, const std::vector<uint8_t>& p) {
    if (inverted) return;
    if (!accept(lsb ? 0 : 1, p.empty() ? nullptr : p.data(), y, rows)) return;
    drv->writeGrayscalePlaneStrip(bus, lsb ? freeink::GrayPlane::Lsb : freeink::GrayPlane::Msb, p.data(), y, rows);
  }
  void grayDisplay(bool off, bool lut, bool factory, const std::vector<uint8_t>& fb) {
    static const unsigned char someLut[1] = {0};
    if (inverted || grayFailed) return;
    if (grayMode == GrayscaleMode::Absolute) {
      if (grayRows[0] != H || grayRows[1] != H || lut) {
        cancelGrayscalePass();
        return;
      }
      drv->displayGray(bus, fb.data(), off, nullptr, true);
      inversionDirty = false;
      cancelGrayscalePass();
      return;
    }
    drv->displayGray(bus, fb.data(), off, lut ? someLut : nullptr, factory);
  }
  void cleanup(const std::vector<uint8_t>& bw) {
    cancelGrayscalePass();
    if (!inverted) drv->cleanupGrayscaleBuffers(bus, bw.size() == SIZE ? bw.data() : nullptr);
  }
  void precondition(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!inverted) drv->preconditionGrayscale(bus, x, y, w, h);
  }
  void setInverted(bool v) {
    if (inverted == v) return;
    cancelGrayscalePass();
    inverted = v;
    inversionDirty = true;
  }
};

// Pixels where the glass does not show what a 1-bit plane says.
uint64_t glassOff(const std::vector<uint8_t>& plane) {
  uint64_t off = 0;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) off += ctl.glass[static_cast<size_t>(y) * W + x] != (bit(plane, x, y) ? WHITE : BLACK);
  return off;
}

void dump(const char* path) {
  FILE* f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P5\n%d %d\n255\n", W, H);
  static const uint8_t shade[5] = {0, 96, 200, 255, 128};
  for (uint8_t g : ctl.glass) fputc(shade[g], f);
  fclose(f);
}

const char* opName(uint8_t op) {
  static const char* names[] = {"?",           "begin", "display",      "gray_base", "gray_base_mode", "copy_lsb",
                                "copy_msb",    "strip", "gray_display", "cleanup",   "precondition",   "deep_sleep",
                                "set_inverted"};
  return op < sizeof(names) / sizeof(names[0]) ? names[op] : "?";
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <trace> [--dump op:file.pgm]...\n", argv[0]);
    return 2;
  }
  std::vector<std::pair<long, std::string>> dumps;
  for (int i = 2; i + 1 < argc; i += 2) {
    if (strcmp(argv[i], "--dump") != 0) return 2;
    const char* colon = strchr(argv[i + 1], ':');
    if (!colon) return 2;
    dumps.emplace_back(strtol(argv[i + 1], nullptr, 10), colon + 1);
  }
  FILE* in = fopen(argv[1], "rb");
  if (!in) {
    perror(argv[1]);
    return 1;
  }
  Facade panel;
  bool begun = false;
  long index = 0;
  uint8_t head[12];
  while (fread(head, 1, sizeof(head), in) == sizeof(head)) {
    if (head[0] != 'T') {
      fprintf(stderr, "bad record %ld\n", index);
      return 1;
    }
    const uint8_t op = head[1], a = head[2], b = head[3];
    const uint16_t c = head[4] | head[5] << 8, d = head[6] | head[7] << 8;
    const uint32_t len = head[8] | head[9] << 8 | head[10] << 16 | static_cast<uint32_t>(head[11]) << 24;
    std::vector<uint8_t> payload(len);
    if (len && fread(payload.data(), 1, len, in) != len) {
      fprintf(stderr, "short record %ld\n", index);
      return 1;
    }
    ctl.refreshes = 0;
    ctl.driven = 0;
    ctl.lastBank = Bank::None;
    const unsigned anomaliesBefore = ctl.anomalies;
    ctl.anomaly.clear();
    if (op != 1 && !begun) {
      fprintf(stderr, "record %ld before any begin\n", index);
      return 1;
    }
    switch (op) {
      case 1:
        panel.begin(a);
        begun = true;
        break;
      case 2:
        panel.display(a, b, payload);
        break;
      case 3:
        panel.grayBase(a, b, payload);
        break;
      case 4:
        panel.grayBaseMode(a, b, c, payload);
        break;
      case 5:
        panel.copy(0, payload);
        break;
      case 6:
        panel.copy(1, payload);
        break;
      case 7:
        panel.strip(a, c, d, payload);
        break;
      case 8:
        panel.grayDisplay(a, b, c, payload);
        break;
      case 9:
        panel.cleanup(payload);
        break;
      case 10:
        if (a)
          panel.precondition(0, 0, W, H);
        else
          panel.precondition(c, payload[0] | payload[1] << 8, d, payload[2] | payload[3] << 8);
        break;
      case 11:
        panel.drv->deepSleep(panel.bus);
        break;
      case 12:
        panel.setInverted(a);
        break;
      default:
        fprintf(stderr, "unknown op %u at %ld\n", op, index);
        return 1;
    }
    uint64_t gray = 0, unknown = 0;
    for (uint8_t g : ctl.glass) {
      gray += g == DARK || g == LIGHT;
      unknown += g == UNKNOWN;
    }
    printf(
        "{\"i\": %ld, \"op\": \"%s\", \"a\": %u, \"b\": %u, \"c\": %u, \"refreshes\": %u, \"bank\": \"%s\", "
        "\"driven\": %llu, \"gray\": %llu, \"unknown\": %llu, \"old_plane_off\": %llu",
        index, opName(op), a, b, c, ctl.refreshes, bankName(ctl.lastBank), static_cast<unsigned long long>(ctl.driven),
        static_cast<unsigned long long>(gray), static_cast<unsigned long long>(unknown),
        static_cast<unsigned long long>(glassOff(ctl.dtm1)));
    if (op == 2 && payload.size() == SIZE) {
      if (panel.inverted)
        for (auto& v : payload) v = ~v;
      printf(", \"glass_vs_frame\": %llu", static_cast<unsigned long long>(glassOff(payload)));
    }
    if (ctl.anomalies != anomaliesBefore) printf(", \"anomaly\": \"%s\"", ctl.anomaly.c_str());
    printf("}\n");
    for (const auto& [at, path] : dumps)
      if (at == index) dump(path.c_str());
    index++;
  }
  return 0;
}
