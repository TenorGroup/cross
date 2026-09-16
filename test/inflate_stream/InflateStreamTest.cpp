#include <BuildScratch.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>

namespace {
size_t allocationAttempts = 0;
size_t liveAllocations = 0;
size_t failOnAttempt = 0;

void* testMalloc(size_t size) {
  if (++allocationAttempts == failOnAttempt) return nullptr;
  void* result = std::malloc(size);
  if (result) ++liveAllocations;
  return result;
}

void testFree(void* ptr) {
  if (ptr) --liveAllocations;
  std::free(ptr);
}
}  // namespace

// Intercept only InflateStream's allocations, leaving the test runner untouched.
#define malloc testMalloc
#define free testFree
#include "../../lib/miniz/src/InflateStream.cpp"
#undef free
#undef malloc

namespace {
constexpr uint8_t COMPRESSED[] = {
    0xed, 0xc9, 0xb1, 0x09, 0xc0, 0x20, 0x10, 0x00, 0xc0, 0x3e, 0x53, 0xb8, 0x8b, 0x0b, 0x64, 0x05, 0x0b, 0x03, 0x42,
    0x50, 0x78, 0xbf, 0x71, 0xfb, 0x8c, 0x91, 0xe6, 0xae, 0xbd, 0x1a, 0x6b, 0xef, 0x7b, 0x8d, 0x99, 0x65, 0xcc, 0xe7,
    0x6d, 0xd9, 0x4b, 0xf4, 0x8c, 0x73, 0x55, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84,
    0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21,
    0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08,
    0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42,
    0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10,
    0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84,
    0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21,
    0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08,
    0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42,
    0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10,
    0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84,
    0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21,
    0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08, 0x21, 0x84, 0x10, 0x42, 0x08,
    0x21, 0xc4, 0xdf, 0xf1, 0x01};
constexpr char LINE[] = "CrossPoint inflate retry\n";
constexpr size_t OUTPUT_SIZE = (sizeof(LINE) - 1) * 4000;

class InflateStreamTest : public ::testing::Test {
 protected:
  alignas(8) inline static std::array<uint8_t, 48000> framebuffer{};

  void SetUp() override {
    allocationAttempts = 0;
    liveAllocations = 0;
    failOnAttempt = 0;
    framebuffer.fill(0);
  }
  void TearDown() override {
    EXPECT_EQ(liveAllocations, 0u);
    buildscratch::reclaim();
  }

  void verifyOutput(InflateStream& stream) {
    stream.setSource(COMPRESSED, sizeof(COMPRESSED));
    std::array<uint8_t, 127> output{};
    size_t total = 0;
    while (total < OUTPUT_SIZE) {
      const size_t wanted = std::min(output.size(), OUTPUT_SIZE - total);
      ASSERT_TRUE(stream.read(output.data(), wanted));
      for (size_t i = 0; i < wanted; ++i) {
        ASSERT_EQ(output[i], static_cast<uint8_t>(LINE[(total + i) % (sizeof(LINE) - 1)]));
      }
      total += wanted;
    }
  }
};

TEST_F(InflateStreamTest, BorrowedFramebufferDecodesAcrossWindowWrapsWithoutHeap) {
  buildscratch::lend(framebuffer.data(), framebuffer.size());
  failOnAttempt = 1;
  for (int pass = 0; pass < 3; ++pass) {
    InflateStream stream;
    ASSERT_TRUE(stream.init(true));
    verifyOutput(stream);
  }
  EXPECT_EQ(allocationAttempts, 0u);
  EXPECT_EQ(buildscratch::claim(framebuffer.size()), framebuffer.data());
  buildscratch::release(framebuffer.data());
}

TEST_F(InflateStreamTest, StateAllocationFailureCanRetry) {
  InflateStream stream;
  failOnAttempt = 1;
  EXPECT_FALSE(stream.init(true));
  EXPECT_EQ(liveAllocations, 0u);
  failOnAttempt = 0;
  ASSERT_TRUE(stream.init(true));
  verifyOutput(stream);
}

TEST_F(InflateStreamTest, WindowAllocationFailureImmediatelyReleasesState) {
  InflateStream stream;
  failOnAttempt = 2;
  EXPECT_FALSE(stream.init(true));
  EXPECT_EQ(liveAllocations, 0u);
  failOnAttempt = 0;
  ASSERT_TRUE(stream.init(true));
  verifyOutput(stream);
}

TEST_F(InflateStreamTest, OccupiedScratchIsNotOverwrittenOnHeapFailure) {
  buildscratch::lend(framebuffer.data(), framebuffer.size());
  ASSERT_EQ(buildscratch::claim(framebuffer.size()), framebuffer.data());
  framebuffer.fill(0xa5);
  InflateStream stream;
  failOnAttempt = 1;
  EXPECT_FALSE(stream.init(true));
  EXPECT_EQ(framebuffer.front(), 0xa5);
  EXPECT_EQ(framebuffer.back(), 0xa5);
  buildscratch::release(framebuffer.data());
  ASSERT_TRUE(stream.init(true));
  verifyOutput(stream);
  EXPECT_EQ(allocationAttempts, 1u);
}
}  // namespace

#include "../../src/components/X3BrandAssets.h"
#include "../../src/components/X3BrandCodec.h"
namespace {
uint32_t planeCrc(const uint8_t* data, size_t size) {
  uint32_t crc = 0xffffffffU;
  while (size--) {
    crc ^= *data++;
    for (int i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
  }
  return ~crc;
}
TEST_F(InflateStreamTest, BrandPlanesDecodeExactlyWithOneAllocation) {
  std::array<uint8_t, x3brand::PLANE_BYTES> out;
  const uint8_t* sources[] = {x3brand::BOOT_LSB, x3brand::BOOT_MSB, x3brand::SLEEP_LSB, x3brand::SLEEP_MSB};
  const size_t sizes[] = {sizeof(x3brand::BOOT_LSB), sizeof(x3brand::BOOT_MSB), sizeof(x3brand::SLEEP_LSB),
                          sizeof(x3brand::SLEEP_MSB)};
  const uint32_t crcs[] = {x3brand::BOOT_LSB_CRC, x3brand::BOOT_MSB_CRC, x3brand::SLEEP_LSB_CRC,
                           x3brand::SLEEP_MSB_CRC};
  for (size_t i = 0; i < 4; ++i) {
    ASSERT_TRUE(decodeX3BrandPlane(sources[i], sizes[i], out.data(), out.size()));
    EXPECT_EQ(planeCrc(out.data(), out.size()), crcs[i]);
    EXPECT_EQ(allocationAttempts, i + 1);
    EXPECT_EQ(liveAllocations, 0u);
  }
}
TEST_F(InflateStreamTest, BrandPlaneRejectsTruncatedAndCorruptInput) {
  std::array<uint8_t, x3brand::PLANE_BYTES> out;
  EXPECT_FALSE(decodeX3BrandPlane(x3brand::BOOT_LSB, sizeof(x3brand::BOOT_LSB) - 1, out.data(), out.size()));
  std::array<uint8_t, sizeof(x3brand::BOOT_LSB)> bad;
  std::copy(std::begin(x3brand::BOOT_LSB), std::end(x3brand::BOOT_LSB), bad.begin());
  bad.back() ^= 1;
  EXPECT_FALSE(decodeX3BrandPlane(bad.data(), bad.size(), out.data(), out.size()));
  EXPECT_FALSE(decodeX3BrandPlane(x3brand::BOOT_LSB, sizeof(x3brand::BOOT_LSB), out.data(), out.size() - 1));
}
TEST_F(InflateStreamTest, BrandPlaneAllocationFailureLeavesNoLeakAndCanRetry) {
  std::array<uint8_t, x3brand::PLANE_BYTES> out;
  failOnAttempt = 1;
  EXPECT_FALSE(decodeX3BrandPlane(x3brand::BOOT_LSB, sizeof(x3brand::BOOT_LSB), out.data(), out.size()));
  EXPECT_EQ(liveAllocations, 0u);
  EXPECT_TRUE(decodeX3BrandPlane(x3brand::BOOT_LSB, sizeof(x3brand::BOOT_LSB), out.data(), out.size()));
}
}  // namespace
