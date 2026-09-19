#include "PersistableStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <RecoverableFile.h>
#include <ObfuscationUtils.h>

#include <cstring>
#include <limits>

bool PersistableStoreBase::writeDocToFile(const char* path, const JsonDocument& doc) {
  if (doc.overflowed()) {
    LOG_ERR("PERSIST", "Incomplete JSON document for %s", path);
    return false;
  }
  const size_t expected = measureJson(doc);
  String json;
  const size_t serialized = serializeJson(doc, json);
  // Arduino String serialization may report bytes attempted when concat fails.
  if (serialized != expected || json.length() != expected) {
    LOG_ERR("PERSIST", "Incomplete JSON serialization for %s", path);
    return false;
  }
  Storage.mkdir("/.crosspoint");
  if (!freeink::recoverFile(Storage, path)) return false;
  const std::string backup = std::string(path) + ".davbak";
  if (Storage.exists(backup.c_str())) {
    // A previous commit may have succeeded while backup cleanup failed. Validate
    // the main copy before retiring the recovery copy on a subsequent save.
    JsonDocument previous;
    if (!readDocFromFile(path, previous)) return false;
    if (Storage.exists(backup.c_str())) {
      // readDocFromFile can serve a backup even when restoring it failed. In
      // that case keep that sole good copy and refuse a replacement attempt.
      previous.clear();
      const String current = Storage.readFile(path);
      if (current.isEmpty() || deserializeJson(previous, current) || previous.overflowed()) return false;
      if (!Storage.remove(backup.c_str())) return false;
    }
  }
  if (!Storage.writeFile(path, json)) {
    LOG_ERR("PERSIST", "Failed to write %s", path);
    return false;
  }
  return true;
}

bool PersistableStoreBase::readDocFromFile(const char* path, JsonDocument& doc) {
  bool unavailable = false;
  const auto parse = [&doc, &unavailable](const char* candidate) {
    unavailable = false;
    if (!Storage.exists(candidate)) return false;
    const String json = Storage.readFile(candidate);
    doc.clear();
    if (json.isEmpty()) {
      // The String API represents both failed reads and a real zero-byte
      // file as empty. Confirm metadata before classifying it as corrupt.
      HalFile probe;
      if (!Storage.openFileForRead("PERSIST", candidate, probe)) {
        unavailable = true;
        return false;
      }
      const bool emptyFile = probe.fileSize() == 0;
      unavailable = !probe.close() || !emptyFile;
      return false;
    }
    const auto error = deserializeJson(doc, json);
    unavailable = error == DeserializationError::NoMemory || doc.overflowed();
    return !error && !unavailable;
  };
  if (parse(path)) return true;
  // A read/allocation failure says nothing about whether main is corrupt.
  // Retry later while preserving both files instead of replacing newer data.
  if (unavailable) return false;
  const std::string backup = std::string(path) + ".davbak";
  if (!parse(backup.c_str())) {
    doc.clear();
    return false;
  }
  // The parsed backup is usable even if SD metadata recovery fails. Retain it
  // on disk so the next boot can retry, and never promote unverified staging.
  if (Storage.exists(path) && !Storage.remove(path)) {
    LOG_ERR("PERSIST", "Using backup; cannot remove invalid %s", path);
    return true;
  }
  if (!Storage.rename(backup.c_str(), path)) LOG_ERR("PERSIST", "Using retained backup for %s", path);
  return true;
}

std::string PersistableStoreBase::extractPassword(JsonVariantConst doc, bool& needsResave) {
  bool valid = false;
  return extractPassword(doc, needsResave, std::numeric_limits<size_t>::max(), valid);
}

std::string PersistableStoreBase::extractPassword(JsonVariantConst doc, bool& needsResave, const size_t maxLength,
                                                  bool& valid) {
  valid = true;
  bool ok = false;
  bool tooLong = false;
  std::string pass = obfuscation::deobfuscateFromBase64(doc["password_obf"] | "", maxLength, &ok, &tooLong);
  if (tooLong) {
    valid = false;
    return "";
  }
  if (!ok) {
    // Deobfuscation failed - fall back to legacy plaintext password.
    const char* legacyPassword = doc["password"] | "";
    const size_t legacyLength = strlen(legacyPassword);
    if (legacyLength > maxLength) {
      valid = false;
      return "";
    }
    pass.assign(legacyPassword, legacyLength);
    if (!pass.empty()) needsResave = true;
  }
  // A successfully decoded empty string is a legitimate value; preserve as-is.
  return pass;
}
