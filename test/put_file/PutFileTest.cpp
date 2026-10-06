#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "platform/PutFile.h"

namespace {
// The port gives the bytes in pieces of `piece`, then nothing.
struct FakePort {
  std::vector<uint8_t> bytes;
  size_t at = 0, piece = 100;
  size_t read(uint8_t* buf, size_t max) {
    const size_t n = std::min({max, piece, bytes.size() - at});
    std::copy_n(bytes.begin() + at, n, buf);
    at += n;
    return n;
  }
};
struct FakeSink {
  std::vector<uint8_t> got;
  size_t failAfter = SIZE_MAX;
  size_t biggestWrite = 0;
  bool write(const uint8_t* b, size_t n) {
    if (got.size() + n > failAfter) return false;
    biggestWrite = std::max(biggestWrite, n);
    got.insert(got.end(), b, b + n);
    return true;
  }
};
// Each call to now() is one millisecond, so a silence is counted in calls to read().
struct FakeClock {
  uint32_t t = 0;
  uint32_t now() { return t++; }
};

std::vector<uint8_t> sample(size_t n) {
  std::vector<uint8_t> v(n);
  for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 31 + 7);
  return v;
}
uint32_t crcOf(const std::vector<uint8_t>& v) { return ~putfile::crcUpdate(0xFFFFFFFFu, v.data(), v.size()); }
}  // namespace

TEST(PutFile, ParsesPathSizeAndCrc) {
  putfile::Command c;
  ASSERT_TRUE(putfile::parse("/v1052/firmware.bin 6000000 DEADbeef", c));
  EXPECT_EQ(c.path, "/v1052/firmware.bin");
  EXPECT_EQ(c.size, 6000000u);
  EXPECT_EQ(c.crc, 0xDEADBEEFu);
}

TEST(PutFile, RefusesMalformedLines) {
  putfile::Command c;
  EXPECT_FALSE(putfile::parse("", c));
  EXPECT_FALSE(putfile::parse("firmware.bin 10 00000000", c));   // no leading slash
  EXPECT_FALSE(putfile::parse("/a 10", c));                      // no crc
  EXPECT_FALSE(putfile::parse("/a ten 00000000", c));            // size not a number
  EXPECT_FALSE(putfile::parse("/a 10 0000", c));                 // crc not 8 digits
  EXPECT_FALSE(putfile::parse("/a 10 0000000g", c));
}

TEST(PutFile, CrcIsZlibs) {
  const std::vector<uint8_t> v{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  EXPECT_EQ(crcOf(v), 0xCBF43926u);
}

TEST(PutFile, WritesEveryByteInSmallPiecesAndAcceptsAMatchingSum) {
  FakePort port{sample(10000)};
  FakeSink sink;
  FakeClock clock;
  uint8_t buf[256];
  const putfile::Command cmd{"/a", 10000, crcOf(port.bytes)};
  EXPECT_EQ(putfile::receive(cmd, port, sink, clock, buf, sizeof(buf), 10000), putfile::Result::Ok);
  EXPECT_EQ(sink.got, port.bytes);
  EXPECT_LE(sink.biggestWrite, sizeof(buf));
}

TEST(PutFile, ReadsNoMoreThanTheSizeOwed) {
  FakePort port{sample(500)};
  port.piece = 400;
  FakeSink sink;
  FakeClock clock;
  uint8_t buf[256];
  const putfile::Command cmd{"/a", 300, 0};
  putfile::receive(cmd, port, sink, clock, buf, sizeof(buf), 10000);
  EXPECT_EQ(sink.got.size(), 300u);  // the 200 after it belong to the next command
}

TEST(PutFile, ASumThatDoesNotMatchIsReported) {
  FakePort port{sample(1000)};
  FakeSink sink;
  FakeClock clock;
  uint8_t buf[256];
  const putfile::Command cmd{"/a", 1000, crcOf(port.bytes) ^ 1u};
  EXPECT_EQ(putfile::receive(cmd, port, sink, clock, buf, sizeof(buf), 10000), putfile::Result::CrcMismatch);
}

TEST(PutFile, ASilenceBeforeTheLastByteIsReported) {
  FakePort port{sample(600)};
  FakeSink sink;
  FakeClock clock;
  uint8_t buf[256];
  const putfile::Command cmd{"/a", 1000, 0};  // 400 bytes never come
  EXPECT_EQ(putfile::receive(cmd, port, sink, clock, buf, sizeof(buf), 10000), putfile::Result::Silence);
  EXPECT_EQ(sink.got.size(), 600u);
}

TEST(PutFile, ACardThatStopsWritingStopsTheTransfer) {
  FakePort port{sample(1000)};
  FakeSink sink;
  sink.failAfter = 300;
  FakeClock clock;
  uint8_t buf[256];
  const putfile::Command cmd{"/a", 1000, crcOf(port.bytes)};
  EXPECT_EQ(putfile::receive(cmd, port, sink, clock, buf, sizeof(buf), 10000), putfile::Result::WriteFailed);
  // The bytes still owed are read and dropped, so none of them reach the command line reader.
  EXPECT_EQ(port.at, port.bytes.size());
  EXPECT_LE(sink.got.size(), 300u);
}
