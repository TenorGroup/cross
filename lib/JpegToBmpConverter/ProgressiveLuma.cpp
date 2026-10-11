#include "ProgressiveLuma.h"

#include <HalStorage.h>
#include <HeapFloor.h>

#include <algorithm>
#include <cstring>
#include <new>

namespace progluma {
namespace {

constexpr uint32_t kSector = 512;
constexpr uint32_t kNone = 0xFFFFFFFFu;
// Zigzag position to natural (row-major) position.
constexpr uint8_t kZigzag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
                                 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
                                 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
                                 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

bool isSof(const uint8_t m) { return m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC; }

Kind kindOf(const uint8_t marker, const int precision, const int height, const int width, const int components) {
  if (marker == 0xC0 || marker == 0xC1) return Kind::Baseline;
  if (marker != 0xC2 || precision != 8 || width <= 0 || height <= 0 || width > kMaxWidth || height > kMaxHeight ||
      (components != 1 && components != 3))
    return Kind::Unsupported;
  return Kind::Progressive;
}

// Canonical Huffman table: maxcode per length, and where each length's values start.
struct Huff {
  uint8_t vals[256];
  int32_t maxcode[17];
  int32_t valoff[17];
};

// True when the 16 counts describe codes that fit their lengths.
bool countsFit(const uint8_t* counts) {
  int32_t code = 0;
  for (int l = 1; l <= 16; ++l) {
    code += counts[l - 1];
    if (code > (1 << l)) return false;
    code <<= 1;
  }
  return true;
}

void build(Huff& h, const uint8_t* counts, const uint8_t* values, const int n) {
  std::memcpy(h.vals, values, n);
  int32_t code = 0, k = 0;
  for (int l = 1; l <= 16; ++l) {
    h.valoff[l] = k - code;
    k += counts[l - 1];
    code += counts[l - 1];
    h.maxcode[l] = counts[l - 1] ? code - 1 : -1;
    code <<= 1;
  }
}

struct Scan {
  uint32_t start = 0;
  uint32_t tableAt[4]{};  // file offset of each table's class/id byte: DC per component, or AC in [0]
  uint8_t ns = 0, comp[4]{}, ss = 0, se = 0, ah = 0, al = 0;
  uint16_t dri = 0;
  bool dcFirst() const { return ss == 0 && ah == 0; }
  int tables() const { return ss > 0 ? 1 : dcFirst() ? ns : 0; }
};

struct Reader {
  Scan scan;
  uint8_t table[4]{};  // pool index of each table this reader decodes with
  uint32_t pos = 0, base = kNone, acc = 0, blocksLeft = 0;
  int nbits = 0, eobrun = 0;
  int32_t pred[4]{};
  uint16_t restartsLeft = 0;
  uint8_t nextRst = 0, fake = 0;
  bool marker = false;
  uint8_t buf[kSector];
};

// One pass over the file in order, through a sector buffer on the stack.
struct Sequence {
  Sequence(HalFile& f, const uint32_t bytes, const Sink& s) : file(f), sink(s), size(bytes) {}
  HalFile& file;
  const Sink& sink;
  uint32_t size, pos = 0, base = kNone;
  bool io = false, stopped = false;
  uint8_t buf[kSector];
  int get() {
    if (pos >= size || stopped) return -1;
    if (base == kNone || pos < base || pos >= base + kSector) {
      // The marker pass reads the whole file: the caller's stop is asked before every sector.
      if (sink.stop && sink.stop(sink.user)) {
        stopped = true;
        return -1;
      }
      base = pos & ~(kSector - 1);
      const uint32_t n = std::min(kSector, size - base);
      if (!file.seek(base) || file.read(buf, n) != static_cast<int>(n)) {
        io = true;
        return -1;
      }
    }
    return buf[pos++ - base];
  }
  int u16() {
    const int a = get(), b = get();
    return a < 0 || b < 0 ? -1 : a << 8 | b;
  }
};

int extend(const int v, const int n) { return n && v < (1 << (n - 1)) ? v - (1 << n) + 1 : v; }
int16_t shifted(const int32_t v, const int al) {
  return static_cast<int16_t>(static_cast<uint16_t>(static_cast<uint32_t>(v) << al));
}

// libjpeg's accurate integer inverse DCT (jidctint.c, islow), to 8 x 8 pixels. The arithmetic wraps
// (unsigned): a valid file never leaves 32 bits, so its pixels are libjpeg's; a damaged file whose
// coefficients do gets a wrong block, never undefined behaviour.
using Wrap = uint32_t;
constexpr Wrap F0298 = 2446, F0390 = 3196, F0541 = 4433, F0765 = 6270, F0899 = 7373, F1175 = 9633, F1501 = 12299,
               F1847 = 15137, F1961 = 16069, F2053 = 16819, F2562 = 20995, F3072 = 25172;
constexpr int CONST_BITS = 13, PASS1_BITS = 2;

// One 1-D pass over in[0..7], descaled by n bits, written every `step` values.
void pass(const Wrap* in, int32_t* out, const int step, const int n) {
  const auto descale = [n](const Wrap x) { return static_cast<int32_t>(x + (Wrap(1) << (n - 1))) >> n; };
  Wrap z2 = in[2], z3 = in[6];
  Wrap z1 = (z2 + z3) * F0541;
  Wrap tmp2 = z1 - z3 * F1847, tmp3 = z1 + z2 * F0765;
  Wrap tmp0 = (in[0] + in[4]) << CONST_BITS, tmp1 = (in[0] - in[4]) << CONST_BITS;
  const Wrap t10 = tmp0 + tmp3, t13 = tmp0 - tmp3, t11 = tmp1 + tmp2, t12 = tmp1 - tmp2;
  tmp0 = in[7];
  tmp1 = in[5];
  tmp2 = in[3];
  tmp3 = in[1];
  z1 = tmp0 + tmp3;
  z2 = tmp1 + tmp2;
  z3 = tmp0 + tmp2;
  Wrap z4 = tmp1 + tmp3;
  const Wrap z5 = (z3 + z4) * F1175;
  tmp0 *= F0298;
  tmp1 *= F2053;
  tmp2 *= F3072;
  tmp3 *= F1501;
  z1 *= Wrap(0) - F0899;
  z2 *= Wrap(0) - F2562;
  z3 = z3 * (Wrap(0) - F1961) + z5;
  z4 = z4 * (Wrap(0) - F0390) + z5;
  tmp0 += z1 + z3;
  tmp1 += z2 + z4;
  tmp2 += z2 + z3;
  tmp3 += z1 + z4;
  out[0] = descale(t10 + tmp3);
  out[7 * step] = descale(t10 - tmp3);
  out[step] = descale(t11 + tmp2);
  out[6 * step] = descale(t11 - tmp2);
  out[2 * step] = descale(t12 + tmp1);
  out[5 * step] = descale(t12 - tmp1);
  out[3 * step] = descale(t13 + tmp0);
  out[4 * step] = descale(t13 - tmp0);
}

void idct(const int16_t* coef, const uint16_t* quant, uint8_t* out, const int stride) {
  int32_t ws[64];
  Wrap in[8];
  for (int c = 0; c < 8; ++c) {
    // A coefficient times its quantizer fits 32 bits (32.767 x 65.535).
    for (int r = 0; r < 8; ++r) in[r] = static_cast<Wrap>(int32_t(coef[r * 8 + c]) * quant[r * 8 + c]);
    if (!in[1] && !in[2] && !in[3] && !in[4] && !in[5] && !in[6] && !in[7]) {
      const auto dc = static_cast<int32_t>(in[0] << PASS1_BITS);
      for (int r = 0; r < 8; ++r) ws[r * 8 + c] = dc;
      continue;
    }
    pass(in, ws + c, 8, CONST_BITS - PASS1_BITS);
  }
  int32_t px[8];
  for (int r = 0; r < 8; ++r) {
    for (int x = 0; x < 8; ++x) in[x] = static_cast<Wrap>(ws[r * 8 + x]);
    pass(in, px, 1, CONST_BITS + PASS1_BITS + 3);
    uint8_t* o = out + r * stride;
    for (int x = 0; x < 8; ++x) o[x] = static_cast<uint8_t>(std::clamp(px[x], int32_t(-128), int32_t(127)) + 128);
  }
}

}  // namespace

// ---- Sniffer ---------------------------------------------------------------------------------

void Sniffer::decide(const Kind kind) {
  result = kind;
  state = State::Known;
}

bool Sniffer::feed(const uint8_t* data, size_t size) {
  for (size_t i = 0; i < size && state != State::Known; ++i) {
    const uint8_t b = data[i];
    switch (state) {
      case State::Start0:
        if (b != 0xFF) decide(Kind::Corrupt); else state = State::Start1;
        break;
      case State::Start1:
        if (b != 0xD8) decide(Kind::Corrupt); else state = State::Fill;
        break;
      case State::Fill:  // bytes between segments other than a marker are skipped, as libjpeg does
        if (b == 0xFF) state = State::Marker;
        break;
      case State::Marker:
        if (b == 0xFF) break;
        if (b == 0xD9 || b == 0xDA) decide(Kind::Corrupt);  // the image ended or started before its frame
        else if (b == 0x00 || b == 0x01 || (b >= 0xD0 && b <= 0xD8)) state = State::Fill;
        else {
          marker = b;
          state = State::Len0;
        }
        break;
      case State::Len0:
        left = uint32_t(b) << 8;
        state = State::Len1;
        break;
      case State::Len1:
        left |= b;
        if (left < 2) { decide(Kind::Corrupt); break; }
        left -= 2;
        if (isSof(marker)) {
          if (left < 6) { decide(Kind::Corrupt); break; }
          got = 0;
          state = State::Frame;
        } else {
          state = left ? State::Skip : State::Fill;
        }
        break;
      case State::Skip: {
        const size_t n = std::min<size_t>(left, size - i);
        left -= n;
        i += n - 1;
        if (!left) state = State::Fill;
        break;
      }
      case State::Frame:
        head[got++] = b;
        if (got == sizeof(head)) {
          frameHeight = head[1] << 8 | head[2];
          frameWidth = head[3] << 8 | head[4];
          decide(kindOf(marker, head[0], frameHeight, frameWidth, head[5]));
        }
        break;
      case State::Known:
        break;
    }
  }
  return known();
}

// ---- Decoder ---------------------------------------------------------------------------------

struct Decoder::State {
  HalFile* file = nullptr;
  uint32_t fileSize = 0;
  int width = 0, height = 0, ncomp = 0, hs[3]{}, vs[3]{}, hmax = 1, vmax = 1;
  int bw = 0, bh = 0, mcux = 0, mcuy = 0, dcw = 0;
  uint16_t quant[64]{};
  int readers = 0, tables = 0;
  uint32_t blocks = 0;
  std::unique_ptr<Reader[]> reader;
  std::unique_ptr<Huff[]> table;
  std::unique_ptr<int16_t[]> dc;    // luma DC of one MCU row
  std::unique_ptr<uint8_t[]> band;  // one block row of pixels
  Result fail = Result::Done;       // Done while nothing went wrong
  const Sink* sink = nullptr;

  void setFail(const Result r) {
    if (fail == Result::Done) fail = r;
  }
  bool stopAsked() {
    if (sink && sink->stop && sink->stop(sink->user)) setFail(Result::Stopped);
    return fail != Result::Done;
  }
  // The next byte of this reader's scan; -1 past the file's end or on a failure.
  int byteAt(Reader& r) {
    if (r.pos >= fileSize || fail != Result::Done) return -1;
    if (r.base == kNone || r.pos < r.base || r.pos >= r.base + kSector) {
      if (stopAsked()) return -1;
      r.base = r.pos & ~(kSector - 1);
      const uint32_t n = std::min(kSector, fileSize - r.base);
      if (!file->seek(r.base) || file->read(r.buf, n) != static_cast<int>(n)) {
        r.base = kNone;
        setFail(Result::Io);
        return -1;
      }
    }
    return r.buf[r.pos++ - r.base];
  }
  void fill(Reader& r) {
    while (r.nbits <= 24) {
      int b = 0;
      if (!r.marker) {
        b = byteAt(r);
        if (b < 0) {
          r.marker = true;
          b = 0;
        } else if (b == 0xFF) {
          int next = byteAt(r);
          while (next == 0xFF) next = byteAt(r);
          if (next != 0) {
            // A marker ends this stretch of data: the reader stays on its 0xFF.
            if (next > 0) r.pos -= 2;
            r.marker = true;
            b = 0;
          }
        }
      }
      // More than the 4 bytes the bit buffer looks ahead: the scan's data ran short.
      if (r.marker && ++r.fake > 4) setFail(Result::Corrupt);
      r.acc |= uint32_t(b) << (24 - r.nbits);
      r.nbits += 8;
    }
  }
  int bits(Reader& r, const int n) {
    if (!n) return 0;
    if (r.nbits < n) fill(r);
    const int v = static_cast<int>(r.acc >> (32 - n));
    r.acc <<= n;
    r.nbits -= n;
    return v;
  }
  int nextBit(Reader& r) { return bits(r, 1); }
  int huff(Reader& r, const Huff& h) {
    if (r.nbits < 16) fill(r);
    int32_t code = 0;
    for (int l = 1; l <= 16; ++l) {
      code = code << 1 | static_cast<int32_t>(r.acc >> 31);
      r.acc <<= 1;
      --r.nbits;
      if (code <= h.maxcode[l]) {
        const int32_t i = h.valoff[l] + code;
        if (i >= 0 && i < 256) return h.vals[i];
        break;
      }
    }
    setFail(Result::Corrupt);
    return 0;
  }
  // Before each MCU (interleaved) or block: the restart marker, when one is due.
  bool restart(Reader& r) {
    if (!r.scan.dri) return true;
    if (r.restartsLeft == 0) {
      r.acc = 0;
      r.nbits = 0;
      r.fake = 0;
      r.marker = false;
      int b = byteAt(r);
      if (b != 0xFF) {
        setFail(Result::Corrupt);
        return false;
      }
      while (b == 0xFF) b = byteAt(r);
      if (b != 0xD0 + (r.nextRst & 7)) {
        setFail(Result::Corrupt);
        return false;
      }
      ++r.nextRst;
      r.eobrun = 0;
      std::memset(r.pred, 0, sizeof(r.pred));
      r.restartsLeft = r.scan.dri;
    }
    --r.restartsLeft;
    return fail == Result::Done;
  }
  void endRun(Reader& r) {
    // An end-of-band run past the scan's last block is a damaged scan.
    if (r.eobrun < 0 || static_cast<uint32_t>(r.eobrun) > r.blocksLeft) setFail(Result::Corrupt);
  }
  void dcFirst(Reader& r, const int k, int16_t* dst) {
    const int t = huff(r, table[r.table[k]]);
    if (t > 15) {
      setFail(Result::Corrupt);
      return;
    }
    const int diff = t ? extend(bits(r, t), t) : 0;
    r.pred[k] = static_cast<int32_t>(static_cast<uint32_t>(r.pred[k]) + static_cast<uint32_t>(diff));
    if (dst) *dst = static_cast<int16_t>(*dst | shifted(r.pred[k], r.scan.al));
  }
  void dcRefine(Reader& r, int16_t* dst) {
    if (nextBit(r) && dst) *dst = static_cast<int16_t>(*dst | (1 << r.scan.al));
  }
  void acFirst(Reader& r, int16_t* coef) {
    if (r.eobrun > 0) {
      --r.eobrun;
      return;
    }
    const Huff& h = table[r.table[0]];
    for (int k = r.scan.ss; k <= r.scan.se; ++k) {
      const int rs = huff(r, h), run = rs >> 4, size = rs & 15;
      if (fail != Result::Done) return;
      if (size) {
        k += run;
        if (k > r.scan.se) {
          setFail(Result::Corrupt);
          return;
        }
        coef[kZigzag[k]] = shifted(extend(bits(r, size), size), r.scan.al);
      } else if (run == 15) {
        k += 15;
      } else {
        r.eobrun = (1 << run) - 1;
        if (run) r.eobrun += bits(r, run);
        endRun(r);
        break;
      }
    }
  }
  void acRefine(Reader& r, int16_t* coef) {
    const int p1 = 1 << r.scan.al, m1 = -p1;
    int k = r.scan.ss;
    const int se = r.scan.se;
    const auto correct = [&](int16_t& t) {
      if (nextBit(r) && !(t & p1)) t = static_cast<int16_t>(t + (t >= 0 ? p1 : m1));
    };
    if (r.eobrun == 0) {
      const Huff& h = table[r.table[0]];
      for (; k <= se; ++k) {
        const int rs = huff(r, h);
        int run = rs >> 4, value = 0;
        if (fail != Result::Done) return;
        if (rs & 15) {
          value = nextBit(r) ? p1 : m1;
        } else if (run != 15) {
          r.eobrun = 1 << run;
          if (run) r.eobrun += bits(r, run);
          break;
        }
        do {
          int16_t& t = coef[kZigzag[k]];
          if (t) correct(t);
          else if (--run < 0) break;
          ++k;
        } while (k <= se);
        if (value) {
          if (k > se) {
            setFail(Result::Corrupt);
            return;
          }
          coef[kZigzag[k]] = static_cast<int16_t>(value);
        }
      }
    }
    if (r.eobrun > 0) {
      for (; k <= se; ++k)
        if (coef[kZigzag[k]]) correct(coef[kZigzag[k]]);
      --r.eobrun;
      endRun(r);
    }
  }
  void block(Reader& r, int16_t* coef) {
    if (!restart(r)) return;
    --r.blocksLeft;
    if (r.scan.ss == 0) r.scan.ah ? dcRefine(r, coef) : dcFirst(r, 0, coef);
    else r.scan.ah ? acRefine(r, coef) : acFirst(r, coef);
  }
};

size_t biggestBlock(const int width) {
  const size_t band = size_t(64) * ((width + 7) / 8);
  return std::max({band, sizeof(Reader) * kMaxReaders, sizeof(Huff) * kMaxReaders * 3});
}
size_t worstBytes(const int width) {
  const size_t blocks = (width + 7) / 8;
  return sizeof(Decoder::State) + sizeof(Reader) * kMaxReaders + sizeof(Huff) * kMaxReaders * 3 +
         sizeof(int16_t) * 4 * (blocks + 4) + 64 * blocks;
}

Decoder::Decoder() = default;
Decoder::~Decoder() = default;
int Decoder::width() const { return s ? s->width : 0; }
int Decoder::height() const { return s ? s->height : 0; }
uint32_t Decoder::blocks() const { return s ? s->blocks : 0; }

namespace {
Result load(Decoder::State& st, HalFile& file, const Sink& sink) {
  st.file = &file;
  st.fileSize = static_cast<uint32_t>(file.size());
  Sequence in(file, st.fileSize, sink);
  const auto bad = [&in](const Result r) { return in.stopped ? Result::Stopped : in.io ? Result::Io : r; };
  if (in.get() != 0xFF || in.get() != 0xD8) return bad(Result::Corrupt);
  uint32_t dcAt[4]{}, acAt[4]{};
  uint16_t quant[4][64]{};
  bool quantSet[4]{}, frame = false;
  int ids[3]{}, tq0 = 0, scans = 0;
  uint16_t dri = 0;
  Scan luma[kMaxReaders];
  bool ended = false;
  for (;;) {
    int b = in.get();
    while (b >= 0 && b != 0xFF) b = in.get();  // stray bytes between segments, skipped as libjpeg does
    while (b == 0xFF) b = in.get();
    ended = b == 0xD9;
    if (b < 0 || ended) break;
    if (b == 0x00 || b == 0x01 || (b >= 0xD0 && b <= 0xD8)) continue;
    const int length = in.u16();
    if (length < 2 || in.pos + length - 2 > st.fileSize) return bad(Result::Corrupt);
    const uint32_t end = in.pos + length - 2;
    const auto marker = static_cast<uint8_t>(b);
    if (isSof(marker)) {
      if (frame || length < 8) return bad(Result::Corrupt);
      const int precision = in.get();
      st.height = in.u16();
      st.width = in.u16();
      st.ncomp = in.get();
      const Kind kind = kindOf(marker, precision, st.height, st.width, st.ncomp);
      if (kind != Kind::Progressive) return bad(kind == Kind::Corrupt ? Result::Corrupt : Result::Unsupported);
      if (length != 8 + 3 * st.ncomp) return bad(Result::Corrupt);
      for (int i = 0; i < st.ncomp; ++i) {
        ids[i] = in.get();
        const int sampling = in.get(), tq = in.get();
        st.hs[i] = sampling >> 4;
        st.vs[i] = sampling & 15;
        if (st.hs[i] < 1 || st.hs[i] > 4 || st.vs[i] < 1 || st.vs[i] > 4 || tq < 0 || tq > 3) return bad(Result::Corrupt);
        if (i == 0) tq0 = tq;
        st.hmax = std::max(st.hmax, st.hs[i]);
        st.vmax = std::max(st.vmax, st.vs[i]);
      }
      // A single component is never interleaved: its blocks are its MCUs.
      if (st.ncomp == 1) st.hs[0] = st.vs[0] = st.hmax = st.vmax = 1;
      // The luma must be the widest plane: a chroma plane wider than it is not decoded here.
      if (st.hs[0] != st.hmax || st.vs[0] != st.vmax) return bad(Result::Unsupported);
      frame = true;
    } else if (marker == 0xC4) {
      while (in.pos < end) {
        const uint32_t at = in.pos;
        const int classId = in.get();
        if (classId < 0 || (classId >> 4) > 1 || (classId & 15) > 3 || in.pos + 16 > end) return bad(Result::Corrupt);
        uint8_t counts[16];
        int n = 0;
        for (auto& c : counts) n += c = static_cast<uint8_t>(in.get());
        if (n > 256 || in.pos + n > end || !countsFit(counts)) return bad(Result::Corrupt);
        in.pos += n;
        (classId >> 4 ? acAt : dcAt)[classId & 15] = at;
      }
    } else if (marker == 0xDB) {
      while (in.pos < end) {
        const int pq = in.get();
        if (pq < 0 || (pq >> 4) > 1 || (pq & 15) > 3 || in.pos + 64u * ((pq >> 4) + 1) > end) return bad(Result::Corrupt);
        for (int k = 0; k < 64; ++k) quant[pq & 15][kZigzag[k]] = static_cast<uint16_t>(pq >> 4 ? in.u16() : in.get());
        quantSet[pq & 15] = true;
      }
    } else if (marker == 0xDD) {
      if (length != 4) return bad(Result::Corrupt);
      dri = static_cast<uint16_t>(in.u16());
    } else if (marker == 0xDA) {
      Scan scan;
      scan.ns = static_cast<uint8_t>(in.get());
      if (!frame || scan.ns < 1 || scan.ns > st.ncomp || length != 6 + 2 * scan.ns) return bad(Result::Corrupt);
      bool hasLuma = false;
      uint8_t td[4]{}, ta[4]{};
      for (int i = 0; i < scan.ns; ++i) {
        const int id = in.get(), tables = in.get();
        int c = 0;
        while (c < st.ncomp && ids[c] != id) ++c;
        if (c == st.ncomp || tables < 0) return bad(Result::Corrupt);
        scan.comp[i] = static_cast<uint8_t>(c);
        hasLuma |= c == 0;
        td[i] = static_cast<uint8_t>(tables >> 4);
        ta[i] = static_cast<uint8_t>(tables & 15);
        if (td[i] > 3 || ta[i] > 3) return bad(Result::Corrupt);
      }
      scan.ss = static_cast<uint8_t>(in.get());
      scan.se = static_cast<uint8_t>(in.get());
      const int approx = in.get();
      if (in.io || in.stopped) return bad(Result::Io);
      scan.ah = static_cast<uint8_t>(approx >> 4);
      scan.al = static_cast<uint8_t>(approx & 15);
      if (scan.se > 63 || scan.ss > scan.se || (scan.ss == 0 && scan.se != 0) || (scan.ss > 0 && scan.ns != 1) ||
          scan.ah > 13 || scan.al > 13 || (scan.ah && scan.al != scan.ah - 1))
        return bad(Result::Corrupt);
      if (++scans > kMaxScans) return bad(Result::Unsupported);
      if (hasLuma) {
        if (st.readers == kMaxReaders) return bad(Result::Unsupported);
        for (int i = 0; i < scan.tables(); ++i) {
          scan.tableAt[i] = scan.ss > 0 ? acAt[ta[0]] : dcAt[td[i]];
          if (!scan.tableAt[i]) return bad(Result::Corrupt);  // a table used before it was defined
        }
        if (!st.readers) {
          if (!quantSet[tq0]) return bad(Result::Corrupt);
          std::memcpy(st.quant, quant[tq0], sizeof(st.quant));
        }
        scan.dri = dri;
        scan.start = end;
        st.tables += scan.tables();
        luma[st.readers++] = scan;
      }
      // Past the entropy-coded data to the next marker (stuffed zeros and restarts belong to it).
      in.pos = end;
      for (;;) {
        int c = in.get();
        if (c < 0) break;
        if (c != 0xFF) continue;
        while (c == 0xFF) c = in.get();
        if (c < 0) break;
        if (c == 0 || (c >= 0xD0 && c <= 0xD7)) continue;
        in.pos -= 2;
        break;
      }
      if (in.io || in.stopped) return bad(Result::Io);
      continue;
    }
    if (in.io || in.stopped) return bad(Result::Io);
    in.pos = end;
  }
  if (in.stopped) return Result::Stopped;
  if (in.io || in.stopped) return bad(Result::Io);
  // A file cut before its end may have lost scans that come after the cut, so it is not decoded.
  if (!frame || !st.readers || !ended) return bad(Result::Corrupt);

  // Memory, through the heap floor door: the readers, their tables, a row of luma DC, a band.
  st.bw = (st.width + 7) / 8;
  st.bh = (st.height + 7) / 8;
  st.mcux = (st.width + 8 * st.hmax - 1) / (8 * st.hmax);
  st.mcuy = (st.height + 8 * st.vmax - 1) / (8 * st.vmax);
  st.dcw = st.mcux * st.hs[0];
  st.reader = heapfloor::array<Reader[]>(st.readers, "pjx-readers");
  st.table = heapfloor::array<Huff[]>(std::max(1, st.tables), "pjx-tables");
  st.dc = heapfloor::array<int16_t[]>(static_cast<size_t>(st.vs[0]) * st.dcw, "pjx-dc");
  st.band = heapfloor::array<uint8_t[]>(static_cast<size_t>(8) * st.bw * 8, "pjx-band");
  if (!st.reader || !st.table || !st.dc || !st.band) return Result::NoHeap;
  int pool = 0;
  for (int i = 0; i < st.readers; ++i) {
    Reader& r = st.reader[i];
    r.scan = luma[i];
    r.pos = r.scan.start;
    r.restartsLeft = r.scan.dri;
    r.blocksLeft = r.scan.ns > 1 ? static_cast<uint32_t>(st.mcux) * st.mcuy : static_cast<uint32_t>(st.bw) * st.bh;
    for (int t = 0; t < r.scan.tables(); ++t) {
      // The table as it stood at this scan: read again where it was defined.
      uint8_t head[17], values[256];
      in.pos = r.scan.tableAt[t];
      for (auto& h : head) h = static_cast<uint8_t>(in.get());
      int n = 0;
      for (int k = 1; k <= 16; ++k) n += head[k];
      for (int k = 0; k < n; ++k) values[k] = static_cast<uint8_t>(in.get());
      if (in.stopped || in.io) return in.stopped ? Result::Stopped : Result::Io;
      build(st.table[pool], head + 1, values, n);
      r.table[t] = static_cast<uint8_t>(pool++);
    }
  }
  return Result::Done;
}
}  // namespace

Result Decoder::open(HalFile& file, const Sink& sink) {
  s.reset(new (std::nothrow) State());
  if (!s) return Result::NoHeap;
  const Result r = load(*s, file, sink);
  if (r != Result::Done) s.reset();  // nothing is held after a refusal
  return r;
}

Result Decoder::run(const Sink& sink) {
  if (!s || !s->reader) return Result::Corrupt;
  State& st = *s;
  st.sink = &sink;
  st.fail = Result::Done;
  const int stride = st.bw * 8;
  int sinceStop = 0;
  const auto due = [&] {
    if (++sinceStop < 16) return false;
    sinceStop = 0;
    return st.stopAsked();
  };
  for (int my = 0; my < st.mcuy; ++my) {
    std::memset(st.dc.get(), 0, sizeof(int16_t) * st.vs[0] * st.dcw);
    for (int i = 0; i < st.readers; ++i) {
      Reader& r = st.reader[i];
      if (r.scan.ns == 1) continue;
      for (int mx = 0; mx < st.mcux; ++mx) {
        if (!st.restart(r)) return st.fail;
        --r.blocksLeft;
        for (int k = 0; k < r.scan.ns; ++k) {
          const int c = r.scan.comp[k];
          for (int v = 0; v < st.vs[c]; ++v)
            for (int h = 0; h < st.hs[c]; ++h) {
              int16_t* dst = c == 0 ? &st.dc[v * st.dcw + mx * st.hs[0] + h] : nullptr;
              r.scan.ah ? st.dcRefine(r, dst) : st.dcFirst(r, k, dst);
            }
        }
        if (st.fail != Result::Done || due()) return st.fail;
      }
    }
    for (int v = 0; v < st.vs[0]; ++v) {
      const int by = my * st.vs[0] + v;
      if (by >= st.bh) break;
      for (int bx = 0; bx < st.bw; ++bx) {
        int16_t coef[64] = {};
        coef[0] = st.dc[v * st.dcw + bx];
        for (int i = 0; i < st.readers; ++i)
          if (st.reader[i].scan.ns == 1) st.block(st.reader[i], coef);
        if (st.fail != Result::Done) return st.fail;
        idct(coef, st.quant, st.band.get() + bx * 8, stride);
        ++st.blocks;
        if (due()) return st.fail;
      }
      for (int y = 0; y < 8 && by * 8 + y < st.height; ++y)
        if (!sink.row(sink.user, st.band.get() + y * stride, by * 8 + y)) return Result::Io;
    }
  }
  return Result::Done;
}

}  // namespace progluma
