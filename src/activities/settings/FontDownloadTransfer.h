#pragma once

#include <FontInstaller.h>
#include <FontPackInstaller.h>
#include <HalStorage.h>
#include <esp_rom_crc.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "network/WebDavReplace.h"

namespace fontdownload {

enum class ManifestFileKind { Invalid, SingleFont, Pack };
enum class TransferResult { OK, Invalid, NoSpace, IoError, NetworkError, ChecksumError, Aborted, InstallError };

struct StreamWriteResult {
  size_t bytes;
  bool complete;
  bool fatal;
  operator bool() const { return complete; }
};

inline ManifestFileKind classifyManifestFile(const char* name) {
  if (FontPackInstaller::isPackFilename(name)) return ManifestFileKind::Pack;
  if (FontInstaller::isValidCpfontRelativePath(name)) return ManifestFileKind::SingleFont;
  return ManifestFileKind::Invalid;
}

inline uint64_t requiredPackSpace(const uint32_t size) { return uint64_t{size} * 2 + 4 * 1024 * 1024; }

inline bool canInstallPack(const uint32_t freeHeap, const uint32_t largestBlock, const size_t workspace) {
  return uint64_t{freeHeap} >= 10240 + uint64_t{workspace} + 1024 &&
         uint64_t{largestBlock} >= 8192 + uint64_t{workspace} + 1024;
}

inline bool packMatchesFamily(const char* name, const char* family) {
  return classifyManifestFile(name) == ManifestFileKind::Pack && family &&
         std::strlen(name) == std::strlen(family) + 11 && std::strncmp(name, family, std::strlen(family)) == 0;
}

template <typename Fetch>
TransferResult streamVerifiedFile(const char* temporary, const uint32_t size, const uint32_t expectedCrc,
                                  bool& cancelled, Fetch fetch) {
  HalFile file;
  if (!Storage.openFileForWrite("FONT", temporary, file)) {
    Storage.remove(temporary);
    return TransferResult::IoError;
  }
  uint32_t received = 0;
  uint32_t crc = 0;
  bool ioError = false;
  bool oversized = false;
  const bool fetched = fetch([&](const uint8_t* bytes, size_t count) -> StreamWriteResult {
    if (cancelled) return {0, false, true};
    if (count > size - received) {
      oversized = true;
      return {0, false, true};
    }
    const size_t reported = file.write(bytes, count);
    size_t accepted = count;
    if (reported != count) {
      ioError = true;
      const size_t position = file.position();
      const bool valid = position >= received && position - received <= count &&
                         (reported == 0 || position - received == reported);
      accepted = valid ? position - received : 0;
#ifdef TENOR_PRESS_PROBE
      LOG_ERR("FONT_WRITE", "offset=%u requested=%u reported=%u accepted=%u retryable=%d",
              received, static_cast<unsigned>(count), static_cast<unsigned>(reported),
              static_cast<unsigned>(accepted), valid);
#endif
      if (!valid) return {0, false, true};
    }
    crc = esp_rom_crc32_le(crc, bytes, static_cast<uint32_t>(accepted));
    received += static_cast<uint32_t>(accepted);
    return {accepted, reported == count, false};
  });
  const bool verified = fetched && !cancelled && received == size && crc == expectedCrc;
  const bool synced = verified && file.sync();
  const bool closed = file.close();
  if (verified && synced && closed) return TransferResult::OK;
  Storage.remove(temporary);
  if (cancelled) return TransferResult::Aborted;
  if (ioError || !closed || (verified && !synced)) return TransferResult::IoError;
  if (oversized || (fetched && !verified)) return TransferResult::ChecksumError;
  return TransferResult::NetworkError;
}

template <typename Fetch, typename Install>
TransferResult downloadAndInstallPack(const char* name, const uint32_t size, const uint32_t crc,
                                     bool& cancelled, Fetch fetch, Install install) {
  if (classifyManifestFile(name) != ManifestFileKind::Pack || size == 0 || size > 128 * 1024 * 1024)
    return TransferResult::Invalid;
  char destination[64];
  char temporary[68];
  std::snprintf(destination, sizeof(destination), "/fonts/%s", name);
  std::snprintf(temporary, sizeof(temporary), "%s.tmp", destination);
  if (Storage.exists(temporary) && !Storage.remove(temporary)) return TransferResult::IoError;
  uint64_t available = 0;
  uint32_t clusterSize = 0;
  if (!Storage.freeSpace(available, clusterSize) || clusterSize == 0) return TransferResult::IoError;
  if (available < requiredPackSpace(size)) return TransferResult::NoSpace;
  if (!Storage.exists("/fonts") && !Storage.mkdir("/fonts")) return TransferResult::IoError;
  if (!webdav::recoverFile(Storage, destination)) return TransferResult::IoError;
  const auto result = streamVerifiedFile(temporary, size, crc, cancelled, fetch);
  if (result != TransferResult::OK) return result;
  if (cancelled) {
    Storage.remove(temporary);
    return TransferResult::Aborted;
  }
  if (!webdav::replaceFile(Storage, temporary, destination)) {
    Storage.remove(temporary);
    return TransferResult::IoError;
  }
  return install(destination) ? TransferResult::OK : TransferResult::InstallError;
}

}
