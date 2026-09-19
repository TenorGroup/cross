#pragma once
#include <string>
#include <RecoverableFile.h>

namespace webdav {
using freeink::recoverFile;
using freeink::replaceFile;
enum class InstallResult { OK, DOWNLOAD_FAILED, VALIDATION_FAILED, REPLACE_FAILED };

// Commit only a completely downloaded, validated file; leave the old file usable on failure.
// backupCleanupPending follows the same contract as replaceFile and stays clear on every
// failure path, including download and validation failures that never reach replacement.
template <typename Store, typename Download, typename Validate>
InstallResult installVerifiedFile(Store& storage, const char* destination, Download download, Validate validate,
                                  bool* backupCleanupPending = nullptr) {
  if (backupCleanupPending) *backupCleanupPending = false;
  const std::string staging = std::string(destination) + ".davtmp";
  InstallResult result = InstallResult::OK;
  if (!download(staging.c_str()))
    result = InstallResult::DOWNLOAD_FAILED;
  else if (!validate(staging.c_str()))
    result = InstallResult::VALIDATION_FAILED;
  else if (!replaceFile(storage, staging.c_str(), destination, backupCleanupPending))
    result = InstallResult::REPLACE_FAILED;
  if (result != InstallResult::OK) storage.remove(staging.c_str());
  return result;
}
}  // namespace webdav
