// The TCF1 session (platform/CableFlash.h) against a fake cable, a fake OTA slot of the size the X4 Pro
// really has (0x7e0000) and a fake clock: what is written, what becomes the boot image, what the host reads.
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <new>
#include <string>
#include <vector>

#include "Image.h"
#include "platform/CableFlash.h"

namespace {
size_t allocations = 0;
bool counting = false;
}  // namespace
void* operator new(size_t n) {
  if (counting) ++allocations;
  if (void* p = std::malloc(n ? n : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
namespace {
// The fakes' own bookkeeping is not the session's.
struct NotCounted {
  bool was = counting;
  NotCounted() { counting = false; }
  ~NotCounted() { counting = was; }
};
}  // namespace

namespace {
constexpr uint32_t kSession = 0x5eed1234;

// The host side of the cable: bytes queued for the device, in pieces of `piece`, then nothing; the
// lines the device answered.
struct FakePort {
  std::deque<uint8_t> bytes;
  size_t piece = 300;
  std::vector<std::string> lines;
  size_t read(uint8_t* buf, size_t max) {
    const size_t n = std::min({max, piece, bytes.size()});
    for (size_t i = 0; i < n; ++i) {
      buf[i] = bytes.front();
      bytes.pop_front();
    }
    return n;
  }
  void reply(const char* line) {
    NotCounted pause;
    lines.emplace_back(line);
  }
  size_t count(const std::string& prefix) const {
    return std::count_if(lines.begin(), lines.end(), [&](const std::string& l) { return l.rfind(prefix, 0) == 0; });
  }
};

struct FakeSlot {
  size_t slotSize = 0x7e0000;
  std::vector<uint8_t> written;
  int begins = 0, ends = 0, boots = 0, aborts = 0, writes = 0;
  bool failEnd = false;
  size_t size() const { return slotSize; }
  bool begin(size_t) {
    ++begins;
    return true;
  }
  bool write(const uint8_t* d, size_t n) {
    NotCounted pause;
    ++writes;
    written.insert(written.end(), d, d + n);
    return true;
  }
  bool end() {
    ++ends;
    return !failEnd;
  }
  bool boot() {
    ++boots;
    return true;
  }
  void abort() { ++aborts; }
};

// Every reading of the clock is one millisecond, so a silence is counted in reads of an empty cable.
struct FakeClock {
  uint32_t t = 0;
  uint32_t now() { return t++; }
};

void put32(std::vector<uint8_t>& v, size_t at, uint32_t x) {
  for (int i = 0; i < 4; ++i) v[at + i] = (x >> (8 * i)) & 0xFF;
}

std::vector<uint8_t> frame(uint8_t type, uint32_t seq, uint32_t offset, const uint8_t* data, size_t len,
                           uint32_t session = kSession) {
  std::vector<uint8_t> f(cableflash::kHeader + len, 0);
  std::copy_n("TCF1", 4, f.begin());
  f[4] = type;
  f[6] = len & 0xFF;
  f[7] = len >> 8;
  put32(f, 8, session);
  put32(f, 12, seq);
  put32(f, 16, offset);
  std::copy_n(data, len, f.begin() + cableflash::kHeader);
  uint32_t crc = putfile::crcUpdate(0xFFFFFFFFu, f.data(), 20);
  crc = ~putfile::crcUpdate(crc, f.data() + cableflash::kHeader, len);
  put32(f, 20, crc);
  return f;
}

std::vector<std::vector<uint8_t>> frames(const std::vector<uint8_t>& image) {
  std::vector<std::vector<uint8_t>> out;
  uint32_t seq = 0;
  for (size_t at = 0; at < image.size(); at += cableflash::kPayload)
    out.push_back(frame(cableflash::kData, seq++, at, image.data() + at,
                        std::min(cableflash::kPayload, image.size() - at)));
  return out;
}

void queue(FakePort& port, const std::vector<uint8_t>& f) { port.bytes.insert(port.bytes.end(), f.begin(), f.end()); }

cableflash::Begin beginFor(const std::vector<uint8_t>& image, const char* version = "v1.0.59") {
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  mbedtls_sha256_update(&sha, image.data(), image.size());
  uint8_t digest[32];
  mbedtls_sha256_finish(&sha, digest);
  std::string line = std::to_string(image.size()) + " ";
  char hex[3];
  for (uint8_t b : digest) {
    std::snprintf(hex, sizeof(hex), "%02x", b);
    line += hex;
  }
  line += std::string(" ") + version;
  cableflash::Begin begin;
  EXPECT_TRUE(cableflash::parseBegin(line.c_str(), begin)) << line;
  return begin;
}

struct Bench {
  FakePort port;
  FakeSlot slot;
  FakeClock clock;
  std::vector<uint8_t> buf = std::vector<uint8_t>(cableflash::kFrame);
  size_t progressCalls = 0, lastProgress = 0;
  cableflash::Result go(const cableflash::Begin& begin, uint16_t chip = 9) {
    allocations = 0;
    counting = true;
    const auto r = cableflash::receive(begin, kSession, chip, port, slot, clock, buf.data(), [&](size_t done, size_t) {
      ++progressCalls;
      lastProgress = done;
    });
    counting = false;
    return r;
  }
};

const uint8_t* none = nullptr;
}  // namespace

TEST(CableSession, AWholeImageIsWrittenOnceAndChosen) {
  const auto image = makeImage(kThisTag);
  Bench run;
  for (const auto& f : frames(image)) queue(run.port, f);
  queue(run.port, frame(cableflash::kEnd, 0, 0, none, 0));
  EXPECT_EQ(run.go(beginFor(image)), cableflash::Result::Ok);
  EXPECT_EQ(run.slot.written, image);
  EXPECT_EQ(run.slot.begins, 1);
  EXPECT_EQ(run.slot.ends, 1);
  EXPECT_EQ(run.slot.boots, 1);
  EXPECT_EQ(run.slot.aborts, 0);
  EXPECT_EQ(run.port.lines.front(), "TCF1 READY 5eed1234 1024");
  EXPECT_EQ(run.port.count("TCF1 ACK "), frames(image).size());
  EXPECT_EQ(run.port.lines.back(), "TCF1 DONE " + std::to_string(image.size()));
  EXPECT_EQ(run.lastProgress, image.size());
  EXPECT_EQ(allocations, 0u);  // the session allocates nothing; the frame buffer is the caller's
}

TEST(CableSession, AnImageLargerThanThePartitionsFileSlotFitsTheRealSlot) {
  // partitions.csv says 0x640000; the X4 Pro's flash holds 0x7e0000. The slot answers, not the file.
  const auto image = makeImage(kThisTag, 9, 0x640000);
  Bench run;
  for (const auto& f : frames(image)) queue(run.port, f);
  queue(run.port, frame(cableflash::kEnd, 0, 0, none, 0));
  EXPECT_EQ(run.go(beginFor(image)), cableflash::Result::Ok);
  EXPECT_EQ(run.slot.boots, 1);
  Bench small;
  small.slot.slotSize = image.size() - 1;
  EXPECT_EQ(small.go(beginFor(image)), cableflash::Result::TooLarge);
  EXPECT_EQ(small.slot.begins, 0);
  EXPECT_EQ(small.port.lines.back(), "TCF1 FAIL too-large");
}

TEST(CableSession, ABrokenFrameIsAskedAgainAndNeverWritten) {
  const auto image = makeImage(kThisTag);
  auto all = frames(image);
  Bench run;
  queue(run.port, all[0]);
  auto bad = all[1];
  bad.back() ^= 0x40;  // one payload bit flipped on the cable
  queue(run.port, bad);
  for (size_t i = 1; i < all.size(); ++i) queue(run.port, all[i]);
  queue(run.port, frame(cableflash::kEnd, 0, 0, none, 0));
  EXPECT_EQ(run.go(beginFor(image)), cableflash::Result::Ok);
  EXPECT_EQ(run.port.count("TCF1 NAK 1 crc"), 1u);
  EXPECT_EQ(run.slot.written, image);
  EXPECT_EQ(run.slot.writes, static_cast<int>(all.size()));
}

TEST(CableSession, AFrameSentTwiceIsAnsweredTwiceAndWrittenOnce) {
  const auto image = makeImage(kThisTag);
  auto all = frames(image);
  Bench run;
  queue(run.port, all[0]);
  queue(run.port, all[0]);  // the host lost the first ACK
  for (size_t i = 1; i < all.size(); ++i) queue(run.port, all[i]);
  queue(run.port, frame(cableflash::kEnd, 0, 0, none, 0));
  EXPECT_EQ(run.go(beginFor(image)), cableflash::Result::Ok);
  EXPECT_EQ(run.port.count("TCF1 ACK 0 1024"), 2u);
  EXPECT_EQ(run.slot.written, image);
}

TEST(CableSession, FramesOutOfOrderOrFromAnotherSessionAreRefused) {
  const auto image = makeImage(kThisTag);
  auto all = frames(image);
  Bench run;
  queue(run.port, all[1]);  // skips the first
  queue(run.port, frame(cableflash::kData, 0, 0, image.data(), 1024, 0x0badf00d));
  // A different frame under a sequence already written is not taken for a resend.
  queue(run.port, all[0]);
  auto other = image;
  other[5] ^= 1;
  queue(run.port, frame(cableflash::kData, 0, 0, other.data(), 1024));
  for (size_t i = 1; i < all.size(); ++i) queue(run.port, all[i]);
  queue(run.port, frame(cableflash::kEnd, 0, 0, none, 0));
  EXPECT_EQ(run.go(beginFor(image)), cableflash::Result::Ok);
  EXPECT_EQ(run.port.count("TCF1 NAK 0 order"), 1u);
  EXPECT_EQ(run.port.count("TCF1 NAK 0 session"), 1u);
  EXPECT_EQ(run.port.count("TCF1 NAK 1 order"), 1u);
  EXPECT_EQ(run.slot.written, image);
}

TEST(CableSession, GarbageOnTheLineIsDrainedThenAskedAgain) {
  const auto image = makeImage(kThisTag);
  auto all = frames(image);
  Bench run;
  const std::string junk = "TCF1 HELLO\n";  // a line where a frame should be
  run.port.bytes.insert(run.port.bytes.end(), junk.begin(), junk.end());
  for (const auto& f : all) queue(run.port, f);
  queue(run.port, frame(cableflash::kEnd, 0, 0, none, 0));
  // The drain takes everything waiting, so the host resends after the NAK: queue the image again
  // the way the host would, after the device has gone quiet.
  struct Host : FakePort {
    std::vector<std::vector<uint8_t>> resend;
    bool sent = false;
    size_t read(uint8_t* buf, size_t max) {
      if (bytes.empty() && !sent && !lines.empty() && lines.back().rfind("TCF1 NAK", 0) == 0) {
        for (const auto& f : resend) bytes.insert(bytes.end(), f.begin(), f.end());
        sent = true;
      }
      return FakePort::read(buf, max);
    }
  } host;
  host.bytes = run.port.bytes;
  host.resend = all;
  host.resend.push_back(frame(cableflash::kEnd, 0, 0, none, 0));
  const auto r = cableflash::receive(beginFor(image), kSession, 9, host, run.slot, run.clock, run.buf.data(),
                                     [](size_t, size_t) {});
  EXPECT_EQ(r, cableflash::Result::Ok);
  EXPECT_EQ(host.count("TCF1 NAK 0 frame"), 1u);
  EXPECT_EQ(run.slot.written, image);
}

TEST(CableSession, ACablePulledHalfwayLeavesTheBootImageAlone) {
  const auto image = makeImage(kThisTag);
  auto all = frames(image);
  for (size_t cut : {size_t(1), all.size() / 2, all.size() - 1}) {
    Bench run;
    for (size_t i = 0; i < cut; ++i) queue(run.port, all[i]);
    run.port.bytes.insert(run.port.bytes.end(), all[cut].begin(), all[cut].begin() + 30);  // mid frame
    EXPECT_EQ(run.go(beginFor(image)), cableflash::Result::Silence);
    EXPECT_EQ(run.slot.boots, 0);
    EXPECT_EQ(run.slot.ends, 0);
    EXPECT_EQ(run.slot.aborts, 1);
    EXPECT_EQ(run.port.lines.back(), "TCF1 FAIL silence");
    EXPECT_GE(run.clock.t, cableflash::kSilenceMs);
  }
}

TEST(CableSession, AnImageForAnotherDeviceIsRefusedBeforeItIsChosen) {
  // X3/X4 (C3) image: refused on its first frame, before a byte is written.
  {
    const auto image = makeImage("CROSSPOINT-BOARD-V1:x4;TENOR-CROSS-VERSION-V1:v1.0.59;", 5);
    Bench run;
    for (const auto& f : frames(image)) queue(run.port, f);
    EXPECT_EQ(run.go(beginFor(image)), cableflash::Result::WrongChip);
    EXPECT_EQ(run.slot.writes, 0);
    EXPECT_EQ(run.slot.boots, 0);
    EXPECT_EQ(run.port.lines.back(), "TCF1 FAIL wrong-chip");
  }
  // The running chip could not be read: nothing is taken on trust.
  {
    const auto image = makeImage(kThisTag);
    Bench run;
    for (const auto& f : frames(image)) queue(run.port, f);
    EXPECT_EQ(run.go(beginFor(image), 0xFFFF), cableflash::Result::WrongChip);
    EXPECT_EQ(run.slot.boots, 0);
  }
  // X4 Classic (same S3 chip): refused as soon as its tag goes by.
  {
    const auto image = makeImage(kX4cTag + "TENOR-CABLE-V1:1;");
    Bench run;
    for (const auto& f : frames(image)) queue(run.port, f);
    EXPECT_EQ(run.go(beginFor(image)), cableflash::Result::WrongBoard);
    EXPECT_EQ(run.slot.boots, 0);
    EXPECT_EQ(run.slot.aborts, 1);
  }
  // No tag at all.
  {
    const auto image = makeImage("not a tenor image");
    Bench run;
    for (const auto& f : frames(image)) queue(run.port, f);
    queue(run.port, frame(cableflash::kEnd, 0, 0, none, 0));
    EXPECT_EQ(run.go(beginFor(image)), cableflash::Result::WrongBoard);
    EXPECT_EQ(run.slot.boots, 0);
  }
}

TEST(CableSession, ADowngradeThatDropsTheChannelIsRefused) {
  const auto image = makeImage(k58Tag);
  Bench run;
  for (const auto& f : frames(image)) queue(run.port, f);
  queue(run.port, frame(cableflash::kEnd, 0, 0, none, 0));
  EXPECT_EQ(run.go(beginFor(image, "v1.0.58")), cableflash::Result::NoCable);
  EXPECT_EQ(run.slot.ends, 0);
  EXPECT_EQ(run.slot.boots, 0);
  EXPECT_EQ(run.slot.aborts, 1);
  EXPECT_EQ(run.port.lines.back(), "TCF1 FAIL no-cable");
  // An older release that keeps the channel is allowed.
  const auto older = makeImage("CROSSPOINT-BOARD-V1:x4pro;TENOR-CROSS-VERSION-V1:v1.0.59;TENOR-CABLE-V1:1;");
  Bench back;
  for (const auto& f : frames(older)) queue(back.port, f);
  queue(back.port, frame(cableflash::kEnd, 0, 0, none, 0));
  EXPECT_EQ(back.go(beginFor(older)), cableflash::Result::Ok);
  EXPECT_EQ(back.slot.boots, 1);
}

TEST(CableSession, TheImageMustBeTheOneAnnouncedAndMustVerify) {
  const auto image = makeImage(kThisTag);
  {
    Bench run;  // the sum from BEGIN is of another image
    for (const auto& f : frames(image)) queue(run.port, f);
    queue(run.port, frame(cableflash::kEnd, 0, 0, none, 0));
    auto begin = beginFor(image);
    begin.sha[0] ^= 1;
    EXPECT_EQ(run.go(begin), cableflash::Result::BadSha);
    EXPECT_EQ(run.slot.boots, 0);
  }
  {
    Bench run;  // the user was asked about another version
    for (const auto& f : frames(image)) queue(run.port, f);
    queue(run.port, frame(cableflash::kEnd, 0, 0, none, 0));
    EXPECT_EQ(run.go(beginFor(image, "v1.0.60")), cableflash::Result::WrongVersion);
    EXPECT_EQ(run.slot.boots, 0);
  }
  {
    Bench run;  // END before the last byte
    auto all = frames(image);
    for (size_t i = 0; i + 1 < all.size(); ++i) queue(run.port, all[i]);
    queue(run.port, frame(cableflash::kEnd, 0, 0, none, 0));
    EXPECT_EQ(run.go(beginFor(image)), cableflash::Result::BadImage);
    EXPECT_EQ(run.slot.boots, 0);
  }
  {
    Bench run;  // the slot does not verify (esp_ota_end): not chosen, not aborted again
    run.slot.failEnd = true;
    for (const auto& f : frames(image)) queue(run.port, f);
    queue(run.port, frame(cableflash::kEnd, 0, 0, none, 0));
    EXPECT_EQ(run.go(beginFor(image)), cableflash::Result::BadImage);
    EXPECT_EQ(run.slot.boots, 0);
    EXPECT_EQ(run.slot.aborts, 0);
  }
  {
    Bench run;  // the host stops it
    auto all = frames(image);
    queue(run.port, all[0]);
    queue(run.port, frame(cableflash::kAbort, 1, 0, none, 0));
    EXPECT_EQ(run.go(beginFor(image)), cableflash::Result::Aborted);
    EXPECT_EQ(run.slot.aborts, 1);
    EXPECT_EQ(run.slot.boots, 0);
  }
}

TEST(CableSession, NoiseInARowEndsTheSession) {
  const auto image = makeImage(kThisTag);
  auto bad = frames(image)[0];
  bad[30] ^= 1;
  Bench run;
  for (int i = 0; i < cableflash::kMaxBadFrames; ++i) queue(run.port, bad);
  EXPECT_EQ(run.go(beginFor(image)), cableflash::Result::Noise);
  EXPECT_EQ(run.slot.writes, 0);
  EXPECT_EQ(run.slot.aborts, 1);
}

TEST(CableSession, BeginLinesAreCheckedBeforeAnythingOpens) {
  cableflash::Begin b;
  const std::string sha(64, 'a');
  EXPECT_TRUE(cableflash::parseBegin(("70000 " + sha + " v1.0.59").c_str(), b));
  EXPECT_EQ(b.size, 70000u);
  EXPECT_EQ(b.sha[0], 0xaa);
  EXPECT_STREQ(b.version, "v1.0.59");
  EXPECT_FALSE(cableflash::parseBegin(("100 " + sha + " v1").c_str(), b));            // under 64 KB
  EXPECT_FALSE(cableflash::parseBegin(("70000 " + sha.substr(1) + " v1").c_str(), b));  // short sum
  EXPECT_FALSE(cableflash::parseBegin(("70000 " + sha.substr(1) + "g v1").c_str(), b));  // not hex
  EXPECT_FALSE(cableflash::parseBegin(("70000 " + sha).c_str(), b));                   // no version
  EXPECT_FALSE(cableflash::parseBegin(("70000 " + sha + " " + std::string(32, 'v')).c_str(), b));
  EXPECT_FALSE(cableflash::parseBegin(("70000 " + sha + " v1 extra").c_str(), b));
  EXPECT_FALSE(cableflash::parseBegin(("-5 " + sha + " v1").c_str(), b));
}

TEST(CableSession, TheMacToolsFrameIsReadAsSent) {
  // tools/nap.py's frame(DATA, 0x5eed1234, 0, 0, b"abc"); test/cable_tool/test_nap.py holds the same bytes.
  const char* hex = "54434631010003003412ed5e0000000000000000297516ab616263";
  Bench bench;
  for (size_t i = 0; hex[i]; i += 2) bench.port.bytes.push_back(static_cast<uint8_t>(std::stoi(std::string(hex + i, 2), nullptr, 16)));
  cableflash::Begin begin;
  begin.size = 70000;
  EXPECT_EQ(bench.go(begin), cableflash::Result::Silence);  // three bytes, then the cable goes quiet
  ASSERT_GE(bench.port.lines.size(), 2u);
  EXPECT_EQ(bench.port.lines[1], "TCF1 ACK 0 3");
  EXPECT_EQ(std::string(bench.slot.written.begin(), bench.slot.written.end()), "abc");
}

// Host time of a whole 6.3 MB session, relative only (cap59/log keeps the numbers).
TEST(CableSession, HostTimeOfAReleaseSizedImage) {
  const auto image = makeImage(kThisTag, 9, 6330000);
  Bench run;
  run.port.piece = 512;
  for (const auto& f : frames(image)) queue(run.port, f);
  queue(run.port, frame(cableflash::kEnd, 0, 0, none, 0));
  const auto t0 = std::chrono::steady_clock::now();
  EXPECT_EQ(run.go(beginFor(image)), cableflash::Result::Ok);
  const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
  std::printf("CABLE_HOST bytes=%zu frames=%zu writes=%d allocations=%zu us=%lld\n", image.size(),
              frames(image).size(), run.slot.writes, allocations, static_cast<long long>(us));
  EXPECT_EQ(allocations, 0u);
}

namespace {
// The X4 Pro's USB CDC as USBCDC::write (framework-arduinoespressif32 cores/esp32/USBCDC.cpp) treats a
// write: a 64 B TinyUSB FIFO, room waited for until the port's timeout, then the rest of the write is
// dropped. The computer drains one 64 B packet per ms while it reads.
struct FakeCdc {
  uint32_t timeoutMs = 250, ms = 0;
  size_t fifo = 0;
  bool hostReads = true;
  int writes = 0;
  std::string wire;
  void setTxTimeoutMs(uint32_t t) { timeoutMs = t; }
  size_t write(const uint8_t* data, size_t n) {
    ++writes;
    const uint32_t until = ms + timeoutMs;
    size_t sent = 0;
    while (sent < n) {
      if (fifo == 64) {
        if (static_cast<int32_t>(ms - until) >= 0) break;
        ++ms;
        if (hostReads) fifo = 0;
        continue;
      }
      wire += static_cast<char>(data[sent++]);
      ++fifo;
    }
    return sent;
  }
};
constexpr const char* kHello =
    "TCF1 HELLO board=x4pro chip=9 version=v1.0.59-rc.1 cable=1 state=valid elf=d1c24df6ec6a9ff3 "
    "running=app0@0x10000+0x7e0000 boot=app0 next=app1@0x7f0000+0x7e0000 frame=1024";
}  // namespace

// Measured 13/10: about 1 HELLO in 2 reached the computer cut at its 64th byte ("...state=vali"), the
// next log line glued on. The whole line goes out in one write and the port's 1 ms comes back after.
TEST(CableLine, AHelloLongerThanTheUsbFifoGoesOutWholeInOneWrite) {
  FakeCdc cdc;
  cdc.timeoutMs = 1;  // main.cpp
  ASSERT_GT(std::string(kHello).size(), 64u);
  EXPECT_TRUE(cableflash::sendLine(cdc, kHello, 1));
  EXPECT_EQ(cdc.wire, std::string(kHello) + "\n");
  EXPECT_EQ(cdc.writes, 1);
  EXPECT_EQ(cdc.timeoutMs, 1u);
}

TEST(CableLine, AHostThatDoesNotReadCostsAtMostTheLineTimeout) {
  FakeCdc cdc;
  cdc.timeoutMs = 1;
  cdc.hostReads = false;
  EXPECT_FALSE(cableflash::sendLine(cdc, kHello, 1));
  EXPECT_LE(cdc.ms, cableflash::kLineMs + 2);
  EXPECT_EQ(cdc.timeoutMs, 1u);
}
