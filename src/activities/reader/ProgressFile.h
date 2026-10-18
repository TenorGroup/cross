#pragma once

#include <HalStorage.h>
#include <Logging.h>
#include <RecoverableFile.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace ProgressFile {

// Writes `len` bytes of reader progress to `<cachePath>/progress.bin` without
// ever leaving the canonical file half-written.
//
// The bytes go to a temporary `progress.bin.tmp` first; only once that is fully
// written and closed is it renamed over progress.bin. An interrupted write
// (power loss or a crash mid-SPI) therefore damages only the throwaway temp file.
// Previously a truncate-in-place write that was cut short left progress.bin with
// a broken FAT cluster chain that the firmware could neither rewrite nor clear,
// stranding the book on an old page (issue #2275).
//
// The replace keeps the old file as a backup until the new one is renamed in, so a
// crash between the directory operations leaves either place, never neither (read
// through openForRead). The point is that progress.bin is never torn or lost.
//
// Note: this prevents corruption on a healthy card going forward. It cannot
// repair an already-corrupted progress.bin -- removing the stale file may itself
// fail at the FAT level, in which case recovery still requires fsck on a host.
//
// Returns true only if the new progress.bin is fully in place.
inline bool writeAtomic(const std::string& cachePath, const uint8_t* data, size_t len) {
  const std::string finalPath = cachePath + "/progress.bin";
  const std::string tmpPath = cachePath + "/progress.bin.tmp";

  {
    HalFile f;
    if (!Storage.openFileForWrite("PRG", tmpPath, f)) {
      LOG_ERR("PRG", "Could not open temp progress file for write: %s", tmpPath.c_str());
      return false;
    }
    const size_t written = f.write(data, len);
    if (written != len) {
      LOG_ERR("PRG", "Short write saving progress to %s: %u/%u bytes", tmpPath.c_str(), (unsigned)written,
              (unsigned)len);
      return false;
    }
    f.flush();
    // f (the temp file) is closed at scope exit (DESTRUCTOR_CLOSES_FILE=1) before
    // the rename below -- SdFat must not rename a path that still has an open FsFile.
  }

  // The old progress.bin stays as progress.bin.davbak until the new one is in place, so a card
  // that dies between the renames leaves the old place for openForRead. A backup found beside
  // progress.bin is what a finished swap left before its cleanup: progress.bin is the newer one.
  const std::string backupPath = finalPath + ".davbak";
  if (!freeink::recoverFile(Storage, finalPath.c_str())) return false;
  if (Storage.exists(backupPath.c_str()) && !Storage.remove(backupPath.c_str())) return false;
  if (!freeink::replaceFile(Storage, tmpPath.c_str(), finalPath.c_str())) {
    LOG_ERR("PRG", "Failed to rename temp progress into place: %s", finalPath.c_str());
    return false;
  }
  return true;
}

// Opens the saved reading place: progress.bin, or the backup a save keeps while it swaps a new
// progress.bin in (writeAtomic), so a card that dies between the two renames still leaves a place.
inline bool openForRead(const char* module, const std::string& cachePath, HalFile& f) {
  const std::string path = cachePath + "/progress.bin";
  return Storage.openFileForRead(module, path, f) || Storage.openFileForRead(module, path + ".davbak", f);
}

}  // namespace ProgressFile
