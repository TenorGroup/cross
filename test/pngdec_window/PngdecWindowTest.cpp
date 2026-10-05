// PNGdec as the firmware builds it (scripts/patch_pngdec.py), under an allocator that refuses any
// single block above a cap. On the X3 the decoder (one ~58 KB block with the zlib window inline)
// failed after a reader session with the radio: 98 KB free, largest block 45 KB. The patch moves
// the 32 KiB window into a block of its own, so no block the decoder asks for exceeds 32 KiB plus
// the object's own ~26 KB, and the decoded pixels stay the same.
#include <PNGdec.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>

#include "octocat.h"

namespace {
size_t cap = SIZE_MAX;
size_t largestAsked = 0;
int live = 0;
int leaked = 0;  // blocks a decode left behind, counted before any assertion allocates

void* capped(size_t bytes) {
  if (bytes > largestAsked) largestAsked = bytes;
  if (bytes > cap) return nullptr;
  void* p = std::malloc(bytes);
  if (p) ++live;
  return p;
}

void release(void* p) {
  if (!p) return;
  --live;
  std::free(p);
}

struct Digest {
  uint64_t hash = 1469598103934665603ULL;
  int rows = 0;
};

int onRow(PNGDRAW* draw) {
  auto* d = static_cast<Digest*>(draw->pUser);
  const int bytes = draw->iWidth * 4;  // octocat is 32 bpp RGBA
  for (int i = 0; i < bytes; ++i) d->hash = (d->hash ^ draw->pPixels[i]) * 1099511628211ULL;
  ++d->rows;
  return 1;
}

int decodeOnce(Digest& digest) {
  std::unique_ptr<PNG> png(new (std::nothrow) PNG());
  if (!png) return -1;
  const int opened = png->openRAM(const_cast<uint8_t*>(octocat), sizeof(octocat), onRow);
  if (opened != PNG_SUCCESS) return opened;
  const int rc = png->decode(&digest, 0);
  png->close();
  return rc;
}

int decodeOctocat(Digest& digest) {
  largestAsked = 0;
  const int before = live;
  const int rc = decodeOnce(digest);
  leaked = live - before;
  return rc;
}

class PngdecWindow : public ::testing::Test {
 protected:
  void SetUp() override { cap = SIZE_MAX; }
  void TearDown() override { cap = SIZE_MAX; }
};
}  // namespace

extern "C" void* pngdec_test_malloc(size_t bytes) { return capped(bytes); }
extern "C" void pngdec_test_free(void* ptr) { release(ptr); }

void* operator new(std::size_t bytes) {
  if (void* p = capped(bytes)) return p;
  throw std::bad_alloc();
}
void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept { return capped(bytes); }
void operator delete(void* p) noexcept { release(p); }
void operator delete(void* p, std::size_t) noexcept { release(p); }

// Pixel hash of the unpatched library's decode, taken once with no cap.
constexpr uint64_t kOctocatHash = 0x94ECC8E1A805579DULL;

TEST_F(PngdecWindow, NoDecoderBlockExceedsThe32KiBWindow) {
  cap = 32 * 1024;
  Digest digest;
  ASSERT_EQ(decodeOctocat(digest), PNG_SUCCESS) << "largest block asked: " << largestAsked;
  EXPECT_EQ(digest.rows, 200);
  EXPECT_EQ(largestAsked, 32u * 1024u);
  EXPECT_LE(sizeof(PNG), 28u * 1024u);
  EXPECT_EQ(leaked, 0);
}

TEST_F(PngdecWindow, PixelsMatchTheUnpatchedDecoder) {
  Digest digest;
  ASSERT_EQ(decodeOctocat(digest), PNG_SUCCESS);
  EXPECT_EQ(digest.rows, 200);
  EXPECT_EQ(digest.hash, kOctocatHash);
  EXPECT_EQ(leaked, 0);
}

TEST_F(PngdecWindow, MissingWindowFailsCleanly) {
  cap = sizeof(PNG);  // the object fits, the 32 KiB window does not
  Digest digest;
  EXPECT_EQ(decodeOctocat(digest), PNG_MEM_ERROR);
  EXPECT_EQ(digest.rows, 0);
  EXPECT_EQ(leaked, 0);
}
