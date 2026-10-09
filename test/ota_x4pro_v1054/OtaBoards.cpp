#include <Arduino.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "FirmwareFlasher.h"
#include "HttpDownloader.h"
#include "OtaUpdater.h"

unsigned long millis() { return 1; }
OtaCalls otaCalls;
static const esp_partition_t slot{0x640000};
static std::vector<uint8_t> image;
static std::string manifest, expectedUrl;
static size_t chunk;
static int downloads = 0;
static bool failWrite = false, failEnd = false;

void require(bool ok, const char* why) {
  if (!ok) throw std::runtime_error(why);
}
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) { return &slot; }
esp_err_t esp_ota_begin(const esp_partition_t*, size_t, esp_ota_handle_t* handle) {
  ++otaCalls.begin;
  *handle = 1;
  return ESP_OK;
}
esp_err_t esp_ota_write(esp_ota_handle_t, const void*, size_t n) {
  if (failWrite) return 1;
  otaCalls.written += n;
  return ESP_OK;
}
esp_err_t esp_ota_abort(esp_ota_handle_t) { ++otaCalls.abort; return ESP_OK; }
esp_err_t esp_ota_end(esp_ota_handle_t) { ++otaCalls.end; return failEnd ? 1 : ESP_OK; }
esp_err_t esp_ota_set_boot_partition(const esp_partition_t*) { ++otaCalls.setBoot; return ESP_OK; }
uint16_t firmware_flash::runningPartitionChipId() {
#if FREEINK_DEVICE_X4PRO
  return 9;
#else
  return 5;
#endif
}
bool HttpDownloader::fetchUrl(const std::string&, const DataCallback& onData, const std::string&,
                              const std::string&, const char*, bool, ProgressCallback, bool*, TransferStats* stats) {
  *stats = {200, true};
  for (size_t at = 0; at < manifest.size(); at += chunk) {
    const size_t n = std::min(chunk, manifest.size() - at);
    if (!onData(reinterpret_cast<const uint8_t*>(manifest.data()) + at, n)) return false;
  }
  return true;
}
bool HttpDownloader::fetchRange(const std::string& url, size_t first, size_t last, const DataCallback& onData,
                                const char*, ProgressCallback, bool*, TransferStats* stats, bool* whole) {
  require(url == expectedUrl, "the updater selected the wrong asset URL");
  ++downloads;
  *whole = false;
  *stats = {206, true};
  const size_t end = std::min(image.size(), last + 1);
  for (size_t at = first; at < end; at += chunk) {
    const size_t n = std::min(chunk, end - at);
    if (!onData(image.data() + at, n)) return false;
    stats->bytes += n;
  }
  return true;
}
std::string read(const std::string& file) {
  std::ifstream input(file, std::ios::binary);
  require(input.good(), "fixture missing");
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
int main(int argc, char** argv) {
  try {
    require(argc == 7, "arguments: folder chunk check install activate dry");
    const std::string folder = argv[1];
    chunk = std::stoul(argv[2]);
    const auto expectedCheck = std::stoi(argv[3]), expectedInstall = std::stoi(argv[4]);
    const bool activate = std::stoi(argv[5]), dry = std::stoi(argv[6]);
    manifest = read(folder + "/manifest.json");
    expectedUrl = read(folder + "/url.txt");
    const auto bytes = read(folder + "/image.bin");
    image.assign(bytes.begin(), bytes.end());
    failWrite = folder.find("write-failure") != std::string::npos;
    failEnd = folder.find("end-failure") != std::string::npos;
    // The digest wrapper must perform SHA-256, independent of the fixtures.
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    mbedtls_sha256_update(&sha, reinterpret_cast<const uint8_t*>("abc"), 3);
    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    const uint8_t known[] = {0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
                            0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    require(std::equal(std::begin(known), std::end(known), digest), "SHA-256 known vector failed");
    OtaUpdater updater;
    if (dry) updater.setDryRun("https://cross.tenor.vn/firmware/local.json");
    const auto check = updater.checkForUpdate();
    require(check == expectedCheck, "unexpected manifest check result");
    int install = -1;
    if (check == OtaUpdater::OK) {
      install = updater.installUpdate();
      std::cout << "observed check=" << check << " install=" << install << " setBoot=" << otaCalls.setBoot << '\n';
      require(install == expectedInstall, "unexpected image install result");
    }
    require(otaCalls.setBoot == int(activate), "unexpected boot slot activation");
    if (check != OtaUpdater::OK || install == OtaUpdater::UPDATE_OLDER_ERROR) {
      require(otaCalls.begin == 0 && downloads == 0, "a rejected offer wrote flash");
    } else if (install == OtaUpdater::OK) {
      require(otaCalls.begin == 1 && otaCalls.end == 1 && otaCalls.abort == 0 && otaCalls.written == image.size(),
              "successful image did not write and close exactly once");
    } else if (install == OtaUpdater::WRONG_DEVICE_ERROR || install == OtaUpdater::INTEGRITY_ERROR) {
      require(otaCalls.abort == 1 && otaCalls.end == 0, "an invalid image reached esp_ota_end");
    }
    std::cout << "PASS check=" << check << " install=" << install << " begin=" << otaCalls.begin
              << " abort=" << otaCalls.abort << " end=" << otaCalls.end << " setBoot=" << otaCalls.setBoot
              << " bytes=" << otaCalls.written << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
