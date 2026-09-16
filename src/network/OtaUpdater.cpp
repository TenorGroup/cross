#include "OtaUpdater.h"

// clang-format off
// HttpDownloader.h pulls Arduino/SdFat, whose macros collide with lwip's
// ip4_addr.h unless seen first. Pin this order; clang-format would otherwise sort
// the local header last and break the build.
#include "HttpDownloader.h"
#include <Logging.h>
#include <Memory.h>
#include <HalClock.h>
#include <mbedtls/sha256.h>
#include <ctime>
#include <ReleaseJsonParser.h>
#include <esp_ota_ops.h>
#include <esp_wifi.h>
// clang-format on

#include <algorithm>
#include <cstring>
#include <string>

#include "FirmwareBoardTag.h"
#include "FirmwareFlasher.h"
#include "OtaPolicy.h"
#include "OtaTrust.h"

namespace {
#ifdef TENOR_OTA_ACCEPTANCE
constexpr char latestReleaseUrl[] = "https://cross.tenor.vn/firmware/acceptance-260915.json";
#else
constexpr char latestReleaseUrl[] = "https://cross.tenor.vn/firmware/stable.json";
#endif
}  // namespace

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  updateAvailable = false;
  latestVersion.clear();
  otaUrl.clear();
  otaSize = totalSize = processedSize = 0;
  if (time(nullptr) < 1735689600 && !halClock.syncFromNTP()) return HTTP_ERROR;
  if (ESP.getFreeHeap() < HttpDownloader::MIN_TLS_FREE_HEAP ||
      ESP.getMaxAllocHeap() < HttpDownloader::MIN_TLS_MAX_ALLOC)
    return OOM_ERROR;

  // The parser owns fixed token/asset buffers. Keep them off the activity stack.
  auto release = makeUniqueNoThrow<ReleaseJsonParser>();
  if (!release) return OOM_ERROR;
  const bool combined = board_tag::boardNameLen() == 2 && memcmp(board_tag::boardName(), "x4", 2) == 0;
  char name[48];
  if (combined)
    snprintf(name, sizeof(name), "tenor-cross-x3-x4.bin");
  else
    snprintf(name, sizeof(name), "tenor-cross-%.*s.bin", static_cast<int>(board_tag::boardNameLen()),
             board_tag::boardName());
  release->setFirmwareAssetName(name);
  size_t received = 0;
  const bool ok = HttpDownloader::fetchUrl(
      latestReleaseUrl,
      [&](const uint8_t* data, size_t len) {
        if (len > 8192 - received) return false;
        received += len;
        release->feed(reinterpret_cast<const char*>(data), len);
        return true;
      },
      "", "", ota_trust::ROOT_CA, false);
  if (!ok) return HTTP_ERROR;
  if (!release->complete() || !release->foundTag()) return JSON_PARSE_ERROR;
  ota_policy::Version version;
  if (!ota_policy::parseVersion(release->getTagName(), version, true)) return JSON_PARSE_ERROR;
  if (!release->foundFirmware()) return NO_UPDATE;
  if (!ota_policy::firmwareUrlAllowed(release->getFirmwareUrl()) ||
      !ota_policy::decodeDigest(release->getFirmwareDigest(), otaDigest) || release->getFirmwareSize() < 24)
    return JSON_PARSE_ERROR;

  latestVersion = release->getTagName();
  otaUrl = release->getFirmwareUrl();
  otaSize = totalSize = release->getFirmwareSize();
  updateAvailable = true;
  LOG_INF("OTA", "Offered %s, %zu bytes", latestVersion.c_str(), otaSize);
  return OK;
}

bool OtaUpdater::isUpdateNewer() const {
  return updateAvailable && ota_policy::stableIsNewer(CROSSPOINT_VERSION, latestVersion.c_str());
}

const std::string& OtaUpdater::getLatestVersion() const { return latestVersion; }

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate(ProgressCallback onProgress, void* ctx) {
  if (!isUpdateNewer()) {
    return UPDATE_OLDER_ERROR;
  }

  // esp_https_ota is hardwired to esp-tls/mbedTLS, whose precompiled build on this
  // package can't negotiate TLS 1.3 (see SecureClient.h). Drive the OTA partition
  // ourselves and stream the firmware through HttpDownloader, which runs over
  // wolfSSL when FREEINK_NET_WOLFSSL is set, reusing its redirect handling for the
  // verified HTTPS transport.
  const esp_partition_t* updatePartition = esp_ota_get_next_update_partition(nullptr);
  if (!updatePartition || otaSize > updatePartition->size || otaSize < 24) {
    LOG_ERR("OTA", "No OTA partition available");
    return INTERNAL_UPDATE_ERROR;
  }

  if (ESP.getFreeHeap() < HttpDownloader::MIN_TLS_FREE_HEAP ||
      ESP.getMaxAllocHeap() < HttpDownloader::MIN_TLS_MAX_ALLOC)
    return OOM_ERROR;
  esp_ota_handle_t otaHandle = 0;
  esp_err_t esp_err = esp_ota_begin(updatePartition, otaSize, &otaHandle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_begin failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  /* For better timing and connectivity, we disable power saving for WiFi */
  esp_wifi_set_ps(WIFI_PS_NONE);

  processedSize = 0;
  int lastReportedPct = -1;
  bool flashOk = true;
  bool sizeOk = true;
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  // The image streams in chunks; only the first bytes carry the header. Buffer
  // the first 14 bytes so we can read chip_id (esp_image_header_t offset 12)
  // and reject a wrong-MCU image before it overwrites the OTA partition.
  uint8_t hdr[14];
  size_t hdrLen = 0;
  bool wrongChip = false;
  // All S3 boards share a chip_id, so also scan the stream for the embedded
  // board tag (FirmwareBoardTag.h). The final integrity check requires a
  // matching tag; a different board aborts the download. The wrong image may partially land in
  // the inactive OTA slot, but esp_ota_abort() below means it never becomes
  // the boot target.
  board_tag::Scanner tagScanner;
  const bool fetchOk = HttpDownloader::fetchUrl(
      otaUrl,
      [&](const uint8_t* data, size_t len) {
        if (len > otaSize - processedSize) {
          sizeOk = false;
          return false;
        }
        if (hdrLen < sizeof(hdr)) {
          const size_t take = std::min(len, sizeof(hdr) - hdrLen);
          std::memcpy(hdr + hdrLen, data, take);
          hdrLen += take;
          if (hdrLen == sizeof(hdr)) {
            uint16_t imageChip;
            std::memcpy(&imageChip, hdr + 12, sizeof(imageChip));
            const uint16_t deviceChip = firmware_flash::runningPartitionChipId();
            if (hdr[0] != 0xE9 || deviceChip == 0xFFFF || imageChip != deviceChip) {
              LOG_ERR("OTA", "wrong chip: image=0x%04X device=0x%04X", imageChip, deviceChip);
              wrongChip = true;
              return false;  // abort the transfer
            }
          }
        }
        tagScanner.feed(data, len);
        if (tagScanner.mismatch()) {
          LOG_ERR("OTA", "wrong board: image=%s device=%.*s", tagScanner.foundName(),
                  static_cast<int>(board_tag::boardNameLen()), board_tag::boardName());
          return false;  // abort the transfer
        }
        if (esp_ota_write(otaHandle, data, len) != ESP_OK) {
          flashOk = false;
          return false;  // abort the transfer
        }
        mbedtls_sha256_update(&sha, data, len);
        processedSize += len;
        // Fire the callback only on whole-percent change. Per-chunk updates wake the
        // render task, whose framebuffer work contends with TLS on the internal arena,
        // and e-ink can't repaint faster than a percent tick anyway.
        if (onProgress && totalSize > 0) {
          const int pct = static_cast<int>(static_cast<uint64_t>(processedSize) * 100 / totalSize);
          if (pct != lastReportedPct) {
            lastReportedPct = pct;
            onProgress(ctx);
          }
        }
        return true;
      },
      "", "", ota_trust::ROOT_CA, false);
  uint8_t digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);

  /* Return back to default power saving for WiFi in case of failing */
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

  if (wrongChip || tagScanner.mismatch()) {
    LOG_ERR("OTA", "Firmware install aborted: wrong device");
    esp_ota_abort(otaHandle);
    return WRONG_DEVICE_ERROR;
  }

  if (!fetchOk || !flashOk) {
    LOG_ERR("OTA", "Firmware install failed (%s)", flashOk ? "download" : "flash write");
    esp_ota_abort(otaHandle);
    return flashOk ? HTTP_ERROR : INTERNAL_UPDATE_ERROR;
  }

  if (!sizeOk || processedSize != otaSize || hdrLen != sizeof(hdr) || memcmp(digest, otaDigest, sizeof(digest)) != 0 ||
      !tagScanner.matched()) {
    LOG_ERR("OTA", "Integrity check failed: received=%zu expected=%zu tagged=%d", processedSize, otaSize,
            tagScanner.matched());
    esp_ota_abort(otaHandle);
    return INTEGRITY_ERROR;
  }
  esp_err = esp_ota_end(otaHandle);  // verifies the written image
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_end failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_ota_set_boot_partition(updatePartition);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_set_boot_partition failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  LOG_INF("OTA", "Update completed");
  return OK;
}
