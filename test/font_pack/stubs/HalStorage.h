#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

struct PowerCut : std::runtime_error { PowerCut() : std::runtime_error("power cut") {} };
inline bool cutDuringStage = false;
inline bool cutAfterBackup = false;
inline bool failPromotion = false;
inline bool failBackupCleanup = false;
inline bool failPackCleanup = false;
inline std::filesystem::path cardRoot;

class HalFile {
 public:
  std::fstream stream;
  std::string name;
  std::filesystem::directory_iterator directory;
  bool opened = false;
  bool folder = false;
  bool open(const std::filesystem::path& path, bool write = false) {
    close();
    name = path.filename().string();
    folder = std::filesystem::is_directory(path);
    if (folder) { directory = std::filesystem::directory_iterator(path); opened = true; return true; }
    stream.open(path, std::ios::binary | (write ? std::ios::out | std::ios::trunc : std::ios::in));
    opened = stream.is_open();
    return opened;
  }
  int read(void* buffer, size_t count) {
    stream.read(static_cast<char*>(buffer), count);
    return static_cast<int>(stream.gcount());
  }
  size_t write(const void* buffer, size_t count) {
    stream.write(static_cast<const char*>(buffer), count);
    if (cutDuringStage && name.ends_with(".cpfont")) { stream.flush(); throw PowerCut(); }
    return stream ? count : 0;
  }
  bool seekSet(size_t position) { stream.clear(); stream.seekg(position); return bool(stream); }
  size_t position() { return static_cast<size_t>(stream.tellg()); }
  uint64_t fileSize64() {
    const auto position = stream.tellg();
    stream.seekg(0, std::ios::end);
    const auto length = stream.tellg();
    stream.seekg(position);
    return static_cast<uint64_t>(length);
  }
  bool sync() { stream.flush(); return bool(stream); }
  bool close() { if (stream.is_open()) stream.close(); opened = false; return true; }
  bool isDirectory() const { return folder; }
  bool isOpen() const { return opened; }
  size_t getName(char* output, size_t size) {
    if (name.size() >= size) return 0;
    std::copy(name.begin(), name.end(), output); output[name.size()] = 0; return name.size();
  }
  HalFile openNextFile() {
    HalFile next;
    if (directory != std::filesystem::directory_iterator{}) { auto path = directory->path(); ++directory; next.open(path); }
    return next;
  }
  explicit operator bool() const { return opened; }
};

struct HostStorage {
  uint64_t available = 1ULL << 30;
  std::filesystem::path path(const char* value) { return cardRoot / std::filesystem::path(value).relative_path(); }
  bool exists(const char* value) { return std::filesystem::exists(path(value)); }
  bool mkdir(const char* value, bool = true) {
    if (std::filesystem::exists(path(value))) return false;
    std::error_code error; std::filesystem::create_directories(path(value), error); return !error;
  }
  bool openFileForRead(const char*, const char* value, HalFile& file) { return file.open(path(value)); }
  bool openFileForWrite(const char*, const char* value, HalFile& file) { return file.open(path(value), true); }
  bool remove(const char* value) {
    if (failPackCleanup && std::string(value).ends_with(".cpfontpack")) return false;
    return std::filesystem::remove(path(value));
  }
  bool removeDir(const char* value) {
    if (failBackupCleanup && std::string(value).ends_with(".old")) return false;
    std::error_code error; std::filesystem::remove_all(path(value), error); return !error;
  }
  bool rename(const char* from, const char* to) {
    if (failPromotion && std::string(from).ends_with("/.staging")) return false;
    std::error_code error; std::filesystem::rename(path(from), path(to), error);
    if (!error && cutAfterBackup && std::string(to).ends_with(".old")) throw PowerCut();
    return !error;
  }
  bool freeSpace(uint64_t& bytes, uint32_t& cluster) { bytes = available; cluster = 4096; return true; }
};
inline HostStorage Storage;
