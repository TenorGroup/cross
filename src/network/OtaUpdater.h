#pragma once

#include <cstdint>
#include <string>

#include "OtaLog.h"

class OtaUpdater {
  bool updateAvailable = false;
  std::string latestVersion;
  std::string otaUrl;
  uint8_t otaDigest[32] = {};
  size_t otaSize = 0;
  size_t processedSize = 0;
  size_t totalSize = 0;
  bool (*cancelCheck)(void* ctx) = nullptr;
  void* cancelCtx = nullptr;
  ota_log::Attempt attempt;
  unsigned long attemptStartMs = 0;
  const char* dryRunManifest = nullptr;

 public:
  using ProgressCallback = void (*)(void* ctx);
  // Asked between reads and while the server is silent; true stops the install.
  using CancelCheck = bool (*)(void* ctx);
  void setCancelCheck(CancelCheck check, void* ctx) {
    cancelCheck = check;
    cancelCtx = ctx;
  }

  enum OtaUpdaterError {
    OK = 0,
    NO_UPDATE,
    HTTP_ERROR,
    JSON_PARSE_ERROR,
    UPDATE_OLDER_ERROR,
    INTERNAL_UPDATE_ERROR,
    OOM_ERROR,
    WRONG_DEVICE_ERROR,
    INTEGRITY_ERROR,
    CANCELLED_ERROR,
  };

  size_t getOtaSize() const { return otaSize; }

  size_t getProcessedSize() const { return processedSize; }

  size_t getTotalSize() const { return totalSize; }

  // How the last check or install ended, for the card log (OtaLog.h).
  const ota_log::Attempt& lastAttempt() const { return attempt; }

  OtaUpdater() = default;
  bool isUpdateNewer() const;
  const std::string& getLatestVersion() const;
  // The probe's dry run: checkForUpdate() reads this manifest (under the firmware directory) instead
  // of the release one, and installUpdate() writes, verifies and closes whatever version it offers,
  // but never switches the boot slot. The caller keeps the string alive.
  void setDryRun(const char* manifestUrl) { dryRunManifest = manifestUrl; }
  bool isDryRun() const { return dryRunManifest != nullptr; }
  OtaUpdaterError checkForUpdate();
  OtaUpdaterError installUpdate(ProgressCallback onProgress = nullptr, void* ctx = nullptr);

 private:
  void startAttempt();
  OtaUpdaterError endAttempt(OtaUpdaterError err, const char* step);
};
