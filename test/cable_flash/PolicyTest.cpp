// The cable channel rule (board_tag::dropsCable) on the X4 Pro, and the card path that does not ask it.
#include <HalStorage.h>
#include <esp_ota_ops.h>
#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "Image.h"
#include "platform/CableFlash.h"
#include "network/FirmwareBoardTag.h"
#include "network/FirmwareFlasher.h"
#include "network/OtaBootSwitch.h"

FakeStorage Storage;
namespace {
const esp_partition_t kApp0{"app0", 0x10000, 0x7e0000};
const esp_partition_t kApp1{"app1", 0x7f0000, 0x7e0000};
int writes = 0, switches = 0;
}  // namespace
const esp_partition_t* esp_ota_get_running_partition() { return &kApp0; }
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) { return &kApp1; }
esp_err_t esp_partition_read(const esp_partition_t*, size_t offset, void* dst, size_t size) {
  const uint8_t header[16] = {0xE9, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, 0, 0, 0};  // the running image: S3
  if (offset + size > sizeof(header)) return 1;
  std::memcpy(dst, header + offset, size);
  return ESP_OK;
}
esp_err_t esp_partition_erase_range(const esp_partition_t*, size_t, size_t) { return ESP_OK; }
esp_err_t esp_partition_write(const esp_partition_t*, size_t, const void*, size_t) {
  ++writes;
  return ESP_OK;
}
bool ota_boot::switchTo(const esp_partition_t*) {
  ++switches;
  return true;
}

namespace {
board_tag::Scanner scan(const std::string& bytes, size_t piece = 1) {
  board_tag::Scanner s;
  for (size_t at = 0; at < bytes.size(); at += piece)
    s.feed(reinterpret_cast<const uint8_t*>(bytes.data()) + at, std::min(piece, bytes.size() - at));
  return s;
}
std::string nul(const std::string& tag) { return std::string("\x01\x02", 2) + tag + std::string(1, '\0') + "xyz"; }
}  // namespace

TEST(CablePolicy, ThisBuildCarriesTheChannelAfterItsVersion) {
  const std::string tag = board_tag::TAG;
  EXPECT_EQ(tag, "CROSSPOINT-BOARD-V1:x4pro;TENOR-CROSS-VERSION-V1:" CROSSPOINT_VERSION ";TENOR-CABLE-V1:1;");
  for (size_t piece : {1, 3, 64}) {
    const auto s = scan(nul(tag), piece);
    EXPECT_TRUE(s.matched());
    EXPECT_TRUE(s.cable());
    EXPECT_TRUE(s.versionIs(CROSSPOINT_VERSION));
    EXPECT_FALSE(s.versionIs("v1.0.5"));
    EXPECT_FALSE(board_tag::dropsCable(s));
  }
}

TEST(CablePolicy, ReleasesWithoutTheChannelAreDropped) {
  EXPECT_TRUE(board_tag::dropsCable(scan(nul(k58Tag))));                       // v1.0.58 X4 Pro
  EXPECT_TRUE(board_tag::dropsCable(scan(nul("no tag at all"))));              // another project
  EXPECT_TRUE(board_tag::dropsCable(scan(nul(k58Tag + "TENOR-CABLE-V1:0;"))));  // a channel it does not have
  EXPECT_TRUE(board_tag::dropsCable(scan(nul(k58Tag + "TENOR-CABLE-V1:1"))));   // cut before its end
  EXPECT_TRUE(board_tag::dropsCable(scan(nul(k58Tag + "XTENOR-CABLE-V1:1;"))));
  // The words elsewhere in the image are not the image's own tag.
  EXPECT_TRUE(board_tag::dropsCable(scan(nul(k58Tag) + "TENOR-CABLE-V1:1;")));
  EXPECT_TRUE(board_tag::dropsCable(scan("TENOR-CABLE-V1:1;" + nul(k58Tag))));
  const auto x4c = scan(nul(kX4cTag + "TENOR-CABLE-V1:1;"));
  EXPECT_TRUE(x4c.mismatch());
  EXPECT_TRUE(board_tag::dropsCable(x4c));
}

// The card is the way to any other firmware (CrossPoint, CrossInk, an older tenor/cross): it takes an image
// without the channel, as every board does. Only the cable session keeps the rule.
TEST(CablePolicy, TheCardPathTakesAnImageWithoutTheChannel) {
  Storage.file = makeImage(k58Tag);
  EXPECT_EQ(firmware_flash::validateImageFile("/old.bin", kApp1.size), firmware_flash::Result::OK);
  Storage.file = makeImage("no tag at all");
  EXPECT_EQ(firmware_flash::validateImageFile("/other.bin", kApp1.size), firmware_flash::Result::OK);
  EXPECT_EQ(firmware_flash::flashFromSdPath("/other.bin", nullptr, nullptr), firmware_flash::Result::OK);
  EXPECT_EQ(switches, 1);
  writes = switches = 0;
  Storage.file = makeImage(kX4cTag);
  EXPECT_EQ(firmware_flash::validateImageFile("/x4c.bin", kApp1.size), firmware_flash::Result::WRONG_BOARD);
  Storage.file = makeImage(kThisTag, 5);
  EXPECT_EQ(firmware_flash::validateImageFile("/x3.bin", kApp1.size), firmware_flash::Result::BAD_CHIP);
  Storage.file = makeImage(kThisTag);
  EXPECT_EQ(firmware_flash::validateImageFile("/new.bin", kApp1.size), firmware_flash::Result::OK);
  EXPECT_EQ(firmware_flash::flashFromSdPath("/new.bin", nullptr, nullptr), firmware_flash::Result::OK);
  EXPECT_GT(writes, 0);
  EXPECT_EQ(switches, 1);
}

TEST(CablePolicy, ScreenDecisionCoversCableEntryAndExit) {
  using cableflash::PolicyAction;
  using cableflash::PolicyInput;
  using cableflash::decidePolicy;
  const auto reader = decidePolicy(PolicyInput{false, true, false, false, false, false, "/book.epub"});
  EXPECT_EQ(reader.action, PolicyAction::Replace);
  EXPECT_STREQ(reader.book, "/book.epub");

  EXPECT_EQ(decidePolicy(PolicyInput{false, false, false, false, false, false, ""}).action,
            PolicyAction::Push);
  const auto failed = decidePolicy(PolicyInput{false, false, false, true, true, true, "/book.epub"});
  EXPECT_EQ(failed.action, PolicyAction::Replace);
  EXPECT_STREQ(failed.book, "/book.epub");
  EXPECT_EQ(decidePolicy(PolicyInput{false, false, false, true, true, false, ""}).action,
            PolicyAction::Reject);
  EXPECT_EQ(decidePolicy(PolicyInput{false, false, true, false, true, false, ""}).action,
            PolicyAction::Reject);
  EXPECT_EQ(decidePolicy(PolicyInput{false, false, false, true, false, true, ""}).action,
            PolicyAction::Reject);

  EXPECT_EQ(decidePolicy(PolicyInput{true, false, false, false, false, false, "/book.epub"}).action,
            PolicyAction::ReturnToBook);
  EXPECT_EQ(decidePolicy(PolicyInput{true, false, false, false, false, false, ""}).action,
            PolicyAction::Finish);
}
