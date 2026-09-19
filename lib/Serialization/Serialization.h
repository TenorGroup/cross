#pragma once
#include <HalStorage.h>

#include <algorithm>
#include <iostream>
#include <limits>
#include <type_traits>

namespace serialization {
template <typename T>
bool writePod(std::ostream& os, const T& value) {
  return static_cast<bool>(os.write(reinterpret_cast<const char*>(&value), sizeof(T)));
}

template <typename T>
bool writePod(HalFile& file, const T& value) {
  return file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T)) == sizeof(T);
}

template <typename T>
bool readPod(std::istream& is, T& value) {
  value = {};
  if constexpr (std::is_same_v<T, bool>) {
    uint8_t raw = 0;
    if (!is.read(reinterpret_cast<char*>(&raw), sizeof(raw)) || raw > 1) return false;
    value = raw != 0;
    return true;
  }
  if (is.read(reinterpret_cast<char*>(&value), sizeof(T))) return true;
  value = {};
  return false;
}

template <typename T>
bool readPod(HalFile& file, T& value) {
  value = {};
  if constexpr (std::is_same_v<T, bool>) {
    uint8_t raw = 0;
    if (file.read(&raw, sizeof(raw)) != sizeof(raw) || raw > 1) return false;
    value = raw != 0;
    return true;
  }
  if (file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T)) == sizeof(T)) return true;
  value = {};
  return false;
}

inline bool writeString(std::ostream& os, const std::string& s) {
  if (s.size() > UINT32_MAX) return false;
  const uint32_t len = s.size();
  return writePod(os, len) && static_cast<bool>(os.write(s.data(), len));
}

inline bool writeString(HalFile& file, const std::string& s) {
  if (s.size() > UINT32_MAX) return false;
  const uint32_t len = s.size();
  return writePod(file, len) && (len == 0 || file.write(reinterpret_cast<const uint8_t*>(s.data()), len) == len);
}

// A cache record reader. Failure is sticky, including seeks and short reads, so
// later fields cannot reuse bytes after an earlier field failed. The caller can
// give the end of its record instead of exposing the rest of a multi-record file.
class CheckedReader {
 public:
  explicit CheckedReader(HalFile& file) : CheckedReader(file, file.size()) {}
  CheckedReader(HalFile& file, size_t end) : file(file), end(std::min(end, file.size())) {}
  bool ok() const { return valid; }
  bool fail() { valid = false; return false; }
  size_t position() const { return file.position(); }
  size_t remaining() const { return position() <= end ? end - position() : 0; }
  bool has(size_t length) const { return valid && position() <= end && length <= end - position(); }
  bool seek(size_t target) {
    valid = valid && target <= end && file.seek(target);
    return valid;
  }
  bool read(void* dst, size_t length) {
    valid = has(length) && (length == 0 || file.read(dst, length) == length);
    return valid;
  }
  template <typename T>
  bool pod(T& value) {
    value = {};
    valid = has(sizeof(T)) && readPod(file, value);
    return valid;
  }
  bool string(std::string& value, size_t maxBytes = 4096) {
    return string(value, maxBytes, [](size_t) { return true; });
  }
  template <typename AllocationCheck>
  bool string(std::string& value, size_t maxBytes, AllocationCheck canAllocate) {
    uint32_t length = 0;
    if (!pod(length) || length > maxBytes || !has(length)) {
      value.clear();
      return fail();
    }
    if (length > value.capacity() && !canAllocate(length)) {
      value.clear();
      return fail();
    }
    value.resize(length);
    if (read(value.data(), length)) return true;
    value.clear();
    return false;
  }

 private:
  HalFile& file;
  size_t end;
  bool valid = true;
};

inline bool readString(std::istream& is, std::string& s, size_t maxBytes = 4096) {
  uint32_t len = 0;
  if (!readPod(is, len) || len > maxBytes) { s.clear(); return false; }
  const auto begin = is.tellg();
  if (begin < 0) { s.clear(); return false; }
  is.seekg(0, std::ios::end);
  const auto end = is.tellg();
  is.seekg(begin);
  if (!is || end < begin || static_cast<uint64_t>(end - begin) < len) { s.clear(); return false; }
  s.resize(len);
  if (len == 0 || is.read(s.data(), len)) return true;
  s.clear();
  return false;
}

inline bool readString(HalFile& file, std::string& s, size_t maxBytes = 4096) {
  CheckedReader reader(file);
  return reader.string(s, maxBytes);
}
}  // namespace serialization
