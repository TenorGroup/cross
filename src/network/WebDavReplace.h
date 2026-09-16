#pragma once
#include <string>

namespace webdav {
// Keep an existing destination recoverable if the final rename fails.
// A leftover backup blocks replacement until recovered from the SD card.
template <typename Store>
bool replaceFile(Store& storage, const char* source, const char* destination) {
  const std::string backup = std::string(destination) + ".davbak";
  if (storage.exists(backup.c_str())) return false;
  const bool existed = storage.exists(destination);
  if (existed && !storage.rename(destination, backup.c_str())) return false;
  if (!storage.rename(source, destination)) {
    if (existed) storage.rename(backup.c_str(), destination);
    return false;
  }
  if (existed) storage.remove(backup.c_str());
  return true;
}
enum class InstallResult { OK, DOWNLOAD_FAILED, VALIDATION_FAILED, REPLACE_FAILED };

// Commit only a completely downloaded, validated file; leave the old file usable on failure.
template <typename Store, typename Download, typename Validate>
InstallResult installVerifiedFile(Store& storage, const char* destination, Download download, Validate validate) {
  const std::string staging = std::string(destination) + ".davtmp";
  InstallResult result = InstallResult::OK;
  if (!download(staging.c_str()))
    result = InstallResult::DOWNLOAD_FAILED;
  else if (!validate(staging.c_str()))
    result = InstallResult::VALIDATION_FAILED;
  else if (!replaceFile(storage, staging.c_str(), destination))
    result = InstallResult::REPLACE_FAILED;
  if (result != InstallResult::OK) storage.remove(staging.c_str());
  return result;
}
}  // namespace webdav
