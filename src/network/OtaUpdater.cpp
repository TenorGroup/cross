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
#include <optional>
#include <string>

#include "FirmwareBoardTag.h"
#include "FirmwareFlasher.h"
#include "OtaPolicy.h"
#include "OtaTrust.h"
#include "TlsRecordSlot.h"

namespace {
#ifdef TENOR_OTA_ACCEPTANCE
constexpr char latestReleaseUrl[] = "https://cross.tenor.vn/firmware/acceptance-260915.json";
#else
constexpr char latestReleaseUrl[] = "https://cross.tenor.vn/firmware/stable.json";
#endif

const char* errorName(const OtaUpdater::OtaUpdaterError err) {
  static const char* const names[] = {"OK",
                                      "NO_UPDATE",
                                      "HTTP_ERROR",
                                      "JSON_PARSE_ERROR",
                                      "UPDATE_OLDER_ERROR",
                                      "INTERNAL_UPDATE_ERROR",
                                      "OOM_ERROR",
                                      "WRONG_DEVICE_ERROR",
                                      "INTEGRITY_ERROR",
                                      "CANCELLED_ERROR"};
  return static_cast<size_t>(err) < sizeof(names) / sizeof(names[0]) ? names[err] : "?";
}

void recordTransfer(ota_log::Attempt& attempt, const HttpDownloader::TransferStats& transfer) {
  attempt.http = transfer.status;
  attempt.bytes = transfer.bytes;
  attempt.total = transfer.total;
  attempt.xferMs = transfer.elapsedMs;
  attempt.idleMs = transfer.idleMs;
  attempt.heap = transfer.heap;
  attempt.largest = transfer.largest;
  attempt.largestMin = transfer.largestMin;
}

// A transfer that failed: where it stopped, and whether the TLS client's idle deadline ended it.
const char* failedTransfer(ota_log::Attempt& attempt, const HttpDownloader::TransferStats& transfer) {
  attempt.wd = ota_log::watchdog(false, transfer.idleMs, HttpDownloader::PINNED_CA_TIMEOUT_MS);
  return ota_log::transferStep(transfer.status, transfer.headers);
}
}  // namespace

void OtaUpdater::startAttempt() {
  attempt = {};
  attemptStartMs = millis();
}

OtaUpdater::OtaUpdaterError OtaUpdater::endAttempt(const OtaUpdaterError err, const char* step) {
  attempt.ok = err == OK;
  attempt.err = errorName(err);
  attempt.step = step;
  attempt.ms = millis() - attemptStartMs;
  if (!attempt.heap) {
    attempt.heap = ESP.getFreeHeap();
    attempt.largest = ESP.getMaxAllocHeap();
  }
  wifi_ap_record_t ap{};
  attempt.rssi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
  return err;
}

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  startAttempt();
  const char* manifestUrl = dryRunManifest;
  updateAvailable = false;
  latestVersion.clear();
  otaUrl.clear();
  otaSize = totalSize = processedSize = 0;
  if (manifestUrl && !ota_policy::manifestUrlAllowed(manifestUrl)) return endAttempt(HTTP_ERROR, "url");
  if (time(nullptr) < 1735689600 && !halClock.syncFromNTP()) return endAttempt(HTTP_ERROR, "ntp");
  if (ESP.getFreeHeap() < HttpDownloader::MIN_TLS_FREE_HEAP ||
      ESP.getMaxAllocHeap() < HttpDownloader::MIN_TLS_MAX_ALLOC)
    return endAttempt(OOM_ERROR, "heap");

  // The parser owns fixed token/asset buffers. Keep them off the activity stack.
  auto release = makeUniqueNoThrow<ReleaseJsonParser>();
  if (!release) return endAttempt(OOM_ERROR, "heap");
  const bool combined = board_tag::boardNameLen() == 2 && memcmp(board_tag::boardName(), "x4", 2) == 0;
  char name[48];
  if (combined)
    snprintf(name, sizeof(name), "tenor-cross-x3-x4.bin");
  else
    snprintf(name, sizeof(name), "tenor-cross-%.*s.bin", static_cast<int>(board_tag::boardNameLen()),
             board_tag::boardName());
  release->setFirmwareAssetName(name);
  size_t received = 0;
  HttpDownloader::TransferStats transfer;
  const bool ok = HttpDownloader::fetchUrl(
      manifestUrl ? manifestUrl : latestReleaseUrl,
      [&](const uint8_t* data, size_t len) {
        if (len > 8192 - received) return false;
        received += len;
        release->feed(reinterpret_cast<const char*>(data), len);
        return true;
      },
      "", "", ota_trust::ROOT_CA, false, nullptr, nullptr, &transfer);
  recordTransfer(attempt, transfer);
  if (!ok) return endAttempt(HTTP_ERROR, failedTransfer(attempt, transfer));
  if (!release->complete() || !release->foundTag()) return endAttempt(JSON_PARSE_ERROR, "parse");
  ota_policy::Version version;
  if (!ota_policy::parseVersion(release->getTagName(), version, true)) return endAttempt(JSON_PARSE_ERROR, "parse");
  if (!release->foundFirmware()) return endAttempt(NO_UPDATE, "parse");
  if (!ota_policy::firmwareUrlAllowed(release->getFirmwareUrl()) ||
      !ota_policy::decodeDigest(release->getFirmwareDigest(), otaDigest) || release->getFirmwareSize() < 24)
    return endAttempt(JSON_PARSE_ERROR, "parse");

  latestVersion = release->getTagName();
  otaUrl = release->getFirmwareUrl();
  otaSize = totalSize = release->getFirmwareSize();
  updateAvailable = true;
  LOG_INF("OTA", "Offered %s, %zu bytes", latestVersion.c_str(), otaSize);
  return endAttempt(OK, "done");
}

bool OtaUpdater::isUpdateNewer() const {
  return updateAvailable && ota_policy::stableIsNewer(CROSSPOINT_VERSION, latestVersion.c_str());
}

const std::string& OtaUpdater::getLatestVersion() const { return latestVersion; }

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate(ProgressCallback onProgress, void* ctx) {
  startAttempt();
  const bool dryRun = isDryRun();
  // The dry run skips only the version rule; everything it writes is still checked below.
  if (dryRun ? !updateAvailable : !isUpdateNewer()) {
    return endAttempt(UPDATE_OLDER_ERROR, "version");
  }

  // esp_https_ota is hardwired to esp-tls/mbedTLS, whose precompiled build on this
  // package can't negotiate TLS 1.3 (see SecureClient.h). Drive the OTA partition
  // ourselves and stream the firmware through HttpDownloader, which runs over
  // wolfSSL when FREEINK_NET_WOLFSSL is set, reusing its redirect handling for the
  // verified HTTPS transport.
  const esp_partition_t* updatePartition = esp_ota_get_next_update_partition(nullptr);
  if (!updatePartition || otaSize > updatePartition->size || otaSize < 24) {
    LOG_ERR("OTA", "No OTA partition available");
    return endAttempt(INTERNAL_UPDATE_ERROR, "begin");
  }

  if (ESP.getFreeHeap() < HttpDownloader::MIN_TLS_FREE_HEAP ||
      ESP.getMaxAllocHeap() < HttpDownloader::MIN_TLS_MAX_ALLOC)
    return endAttempt(OOM_ERROR, "heap");
  esp_ota_handle_t otaHandle = 0;
  esp_err_t esp_err = esp_ota_begin(updatePartition, otaSize, &otaHandle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_begin failed: %s", esp_err_to_name(esp_err));
    return endAttempt(INTERNAL_UPDATE_ERROR, "begin");
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
  bool cancelled = false;
  HttpDownloader::TransferStats transfer;
  // One block serves every 16 KB record of the image: re-allocating it per record let the heap
  // fall to pieces mid-download (TlsRecordSlot.h). With the image in parts no record is that big;
  // the slot stays for a server that sends the whole body.
  std::optional<tls_slot::Scope> recordSlot;
  recordSlot.emplace();
  const HttpDownloader::DataCallback write = [&](const uint8_t* data, size_t len) {
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
  };
  // The server sends each new connection's first ~220 KB in 1.4 and 4.2 KB TLS records and 16 KB
  // records after that; a 16 KB record needs a ~16.4 KB buffer in one piece, and on the X3 the
  // update boot did not have it at the first such record in 10 dry runs of 10 (all stopped at
  // 219-254 KB). So the image comes in parts of PART_BYTES, each over a connection of its own, and
  // every record stays small. A part that breaks is asked again from the byte it reached.
  bool fetchOk = true;
  parts = retries = 0;
  uint32_t elapsedMs = 0;
  uint32_t largestMin = 0;
  while (processedSize < otaSize && !cancelled) {
    const size_t first = processedSize;
    const size_t last = std::min(otaSize, first + PART_BYTES) - 1;
    HttpDownloader::TransferStats part;
    bool whole = false;
    const bool ok = HttpDownloader::fetchRange(
        otaUrl, first, last, write, ota_trust::ROOT_CA,
        [&](size_t, size_t) {
          if (cancelCheck && cancelCheck(cancelCtx)) cancelled = true;
        },
        &cancelled, &part, &whole);
    ++parts;
    elapsedMs += part.elapsedMs;
    if (part.largestMin && (!largestMin || part.largestMin < largestMin)) largestMin = part.largestMin;
    transfer = part;
    if (ok && processedSize > first) continue;
    // A whole part with nothing new: the server has no more bytes; the size check below says so.
    if (ok && !whole) break;
    // What the stream held was wrong, or the user left: asking again cannot help.
    if (cancelled || wrongChip || tagScanner.mismatch() || !flashOk || !sizeOk || whole) {
      fetchOk = false;
      break;
    }
    if (++retries > MAX_PART_RETRIES) {
      fetchOk = false;
      break;
    }
    LOG_INF("OTA", "Part from %u broke at %u, asking again (%u)", static_cast<unsigned>(first),
            static_cast<unsigned>(processedSize), static_cast<unsigned>(retries));
  }
  transfer.bytes = processedSize;
  transfer.total = otaSize;
  transfer.elapsedMs = elapsedMs;
  transfer.largestMin = largestMin;
  recordSlot.reset();
  recordTransfer(attempt, transfer);
  if (!attempt.total) attempt.total = otaSize;
  uint8_t digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);

  /* Return back to default power saving for WiFi in case of failing */
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

  if (cancelled) {
    // The partly written slot is released; the boot partition was never touched.
    LOG_INF("OTA", "Firmware install cancelled at %zu bytes", processedSize);
    esp_ota_abort(otaHandle);
    attempt.wd = ota_log::watchdog(true, transfer.idleMs, HttpDownloader::PINNED_CA_TIMEOUT_MS);
    return endAttempt(CANCELLED_ERROR, ota_log::transferStep(transfer.status, transfer.headers));
  }

  if (wrongChip || tagScanner.mismatch()) {
    LOG_ERR("OTA", "Firmware install aborted: wrong device");
    esp_ota_abort(otaHandle);
    return endAttempt(WRONG_DEVICE_ERROR, "verify");
  }

  if (!fetchOk || !flashOk) {
    LOG_ERR("OTA", "Firmware install failed (%s)", flashOk ? "download" : "flash write");
    esp_ota_abort(otaHandle);
    if (!flashOk) return endAttempt(INTERNAL_UPDATE_ERROR, "flash");
    return endAttempt(HTTP_ERROR, sizeOk ? failedTransfer(attempt, transfer) : "verify");
  }

  if (!sizeOk || processedSize != otaSize || hdrLen != sizeof(hdr) || memcmp(digest, otaDigest, sizeof(digest)) != 0 ||
      !tagScanner.matched()) {
    LOG_ERR("OTA", "Integrity check failed: received=%zu expected=%zu tagged=%d", processedSize, otaSize,
            tagScanner.matched());
    esp_ota_abort(otaHandle);
    return endAttempt(INTEGRITY_ERROR, "verify");
  }
  esp_err = esp_ota_end(otaHandle);  // verifies the written image
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_end failed: %s", esp_err_to_name(esp_err));
    return endAttempt(INTERNAL_UPDATE_ERROR, "end");
  }
  // The verified image stays in the spare slot; the running firmware keeps booting.
  if (dryRun) return endAttempt(OK, "done");

  esp_err = esp_ota_set_boot_partition(updatePartition);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_set_boot_partition failed: %s", esp_err_to_name(esp_err));
    return endAttempt(INTERNAL_UPDATE_ERROR, "set_boot");
  }

  LOG_INF("OTA", "Update completed");
  return endAttempt(OK, "done");
}
