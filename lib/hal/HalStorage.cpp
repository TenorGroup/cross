#include "HalStorage.h"

#include <FS.h>  // need to be included before SdFat.h for compatibility with FS.h's File class
#include <Logging.h>
#include <SDCardManager.h>
#if FREEINK_CAP_USB_MSC
#include <UsbMassStorage.h>
#endif

#include <cassert>
#ifdef TENOR_UI_ACCEPTANCE
#include <esp_timer.h>
#endif

#define SDCard SDCardManager::getInstance()

namespace {
#if FREEINK_CAP_USB_MSC
freeink::UsbMassStorage usbMassStorage;
#endif
#ifdef TENOR_UI_ACCEPTANCE
uint32_t nextFileTraceId = 0;  // Accessed only while StorageLock is held.
#endif
}  // namespace

HalStorage HalStorage::instance;

HalStorage::HalStorage() {
  // Recursive so the same task can re-enter StorageLock without self-deadlock.
  // openFileForRead/Write take the lock and then assign to a HalFile&
  // out-param; if that out-param already held an Impl, its destructor takes
  // the lock again to close the prior FsFile under serialization (see
  // HalFile::Impl::~Impl below). Priority inheritance still applies to
  // recursive mutexes.
  storageMutex = xSemaphoreCreateRecursiveMutex();
  assert(storageMutex != nullptr);
}

// begin() and ready() are only called from setup, no need to acquire mutex for them

bool HalStorage::begin() { return SDCard.begin(); }

bool HalStorage::ready() const { return SDCard.ready(); }

// For the rest of the methods, we acquire the mutex to ensure thread safety

class HalStorage::StorageLock {
 public:
  StorageLock() { xSemaphoreTakeRecursive(HalStorage::getInstance().storageMutex, portMAX_DELAY); }
  ~StorageLock() { xSemaphoreGiveRecursive(HalStorage::getInstance().storageMutex); }
};

void HalStorage::prepareForDeepSleep() {
  StorageLock lock;
  SDCard.shutdown();
}

#if FREEINK_CAP_USB_MSC && !FREEINK_SD_SDMMC
#error "USB Drive requires an SDMMC-backed storage profile"
#endif

bool HalStorage::beginUsbDrive() {
#if FREEINK_CAP_USB_MSC
  StorageLock lock;
  auto* const blockDevice = SDCard.detachFilesystemForRawAccess();
  if (!blockDevice) {
    LOG_ERR("USB", "USB Drive requires a mounted SDMMC filesystem");
    return false;
  }

  if (!usbMassStorage.begin(blockDevice)) {
    LOG_ERR("USB", "USB Drive MSC initialization failed");
    if (!SDCard.begin()) {
      LOG_ERR("USB", "Unable to remount SD card after USB Drive startup failure");
    }
    return false;
  }
  return true;
#else
  return false;
#endif
}

bool HalStorage::disconnectUsbDriveHost() {
#if FREEINK_CAP_USB_MSC
  StorageLock lock;
  return usbMassStorage.disconnectHost();
#else
  return false;
#endif
}

void HalStorage::endUsbDrive() {
#if FREEINK_CAP_USB_MSC
  StorageLock lock;
  usbMassStorage.end();
#endif
}

UsbDriveState HalStorage::usbDriveState() const {
#if FREEINK_CAP_USB_MSC
  StorageLock lock;
  switch (usbMassStorage.state()) {
    case freeink::UsbMassStorageState::WaitingForHost:
      return UsbDriveState::WaitingForHost;
    case freeink::UsbMassStorageState::Connected:
    case freeink::UsbMassStorageState::Accessed:
      return UsbDriveState::Connected;
    case freeink::UsbMassStorageState::Ejected:
      return UsbDriveState::Ejected;
    case freeink::UsbMassStorageState::Disconnected:
      return UsbDriveState::Disconnected;
    case freeink::UsbMassStorageState::IoError:
      return UsbDriveState::IoError;
    case freeink::UsbMassStorageState::Idle:
      break;
  }
#endif
  return UsbDriveState::Unsupported;
}

#define HAL_STORAGE_WRAPPED_CALL(method, ...) \
  HalStorage::StorageLock lock;               \
  return SDCard.method(__VA_ARGS__);

std::vector<String> HalStorage::listFiles(const char* path, int maxFiles) {
  HAL_STORAGE_WRAPPED_CALL(listFiles, path, maxFiles);
}

String HalStorage::readFile(const char* path) { HAL_STORAGE_WRAPPED_CALL(readFile, path); }

bool HalStorage::readFileToStream(const char* path, Print& out, size_t chunkSize) {
  HAL_STORAGE_WRAPPED_CALL(readFileToStream, path, out, chunkSize);
}

size_t HalStorage::readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes) {
  HAL_STORAGE_WRAPPED_CALL(readFileToBuffer, path, buffer, bufferSize, maxBytes);
}

bool HalStorage::writeFile(const char* path, const String& content) {
  HAL_STORAGE_WRAPPED_CALL(writeFile, path, content);
}

bool HalStorage::ensureDirectoryExists(const char* path) { HAL_STORAGE_WRAPPED_CALL(ensureDirectoryExists, path); }

class HalFile::Impl {
 public:
#ifdef TENOR_UI_ACCEPTANCE
  Impl(FsFile&& fsFile, const char* module, const char* path, const char* mode, bool opened,
       unsigned flags = 0, uint32_t parentId = 0)
      : file(std::move(fsFile)), traceId(++nextFileTraceId), openedAtUs(esp_timer_get_time()) {
    LOG_INF("SD_TRACE", "OPEN t_us=%lld id=%u parent=%u mode=%s flags=0x%x ok=%u module=%s path=%s",
            static_cast<long long>(openedAtUs), traceId, parentId, mode, flags, opened ? 1u : 0u,
            module ? module : "", path ? path : "");
  }
#else
  Impl(FsFile&& fsFile) : file(std::move(fsFile)) {}
#endif
  // SdFat is not thread-safe; FsFile::close() touches SD/SPI and must run
  // under StorageLock or it races SdSpiCard::m_spiActive across tasks and
  // trips FreeRTOS's xTaskPriorityDisinherit assert. The FsFile member
  // destructor (DESTRUCTOR_CLOSES_FILE=1) will close() again after the lock
  // releases, but close() on an already-closed FsFile is a no-op. See SdFat
  // issue #518.
  ~Impl() {
    HalStorage::StorageLock lock;
#ifdef TENOR_UI_ACCEPTANCE
    const bool closeOk = file.close();
    traceClose("destructor", closeOk);
#else
    file.close();
#endif
  }
  FsFile file;
#ifdef TENOR_UI_ACCEPTANCE
  const uint32_t traceId;
  const int64_t openedAtUs;
  uint64_t readBytes = 0, writeBytes = 0;
  uint64_t readUs = 0, writeUs = 0, seekUs = 0, syncUs = 0;
  uint32_t readCalls = 0, writeCalls = 0, seekCalls = 0, syncCalls = 0, flushCalls = 0;
  uint32_t readShort = 0, writeShort = 0, readErrors = 0, writeErrors = 0;
  uint32_t seekErrors = 0, syncErrors = 0;
  bool traceClosed = false;

  void recordRead(int result, size_t requested, int64_t started) {
    ++readCalls;
    readUs += static_cast<uint64_t>(esp_timer_get_time() - started);
    if (result > 0) readBytes += static_cast<unsigned>(result);
    if (result < 0) ++readErrors;
    if (result >= 0 && static_cast<size_t>(result) < requested) ++readShort;
  }
  void recordWrite(size_t result, size_t requested, int64_t started) {
    ++writeCalls;
    writeUs += static_cast<uint64_t>(esp_timer_get_time() - started);
    writeBytes += result;
    if (result < requested) {
      ++writeShort;
      ++writeErrors;
    }
  }
  void recordSeek(bool result, int64_t started) {
    ++seekCalls;
    seekUs += static_cast<uint64_t>(esp_timer_get_time() - started);
    if (!result) ++seekErrors;
  }
  void recordSync(bool result, int64_t started) {
    ++syncCalls;
    syncUs += static_cast<uint64_t>(esp_timer_get_time() - started);
    if (!result) ++syncErrors;
  }
  void traceClose(const char* via, bool closeOk) {
    if (traceClosed) return;
    traceClosed = true;
    const int64_t now = esp_timer_get_time();
    LOG_INF("SD_TRACE", "CLOSE t_us=%lld id=%u via=%s ok=%u age_us=%llu r_calls=%u r_bytes=%llu w_calls=%u w_bytes=%llu",
            static_cast<long long>(now), traceId, via, closeOk ? 1u : 0u,
            static_cast<unsigned long long>(now - openedAtUs), readCalls,
            static_cast<unsigned long long>(readBytes), writeCalls,
            static_cast<unsigned long long>(writeBytes));
    LOG_INF("SD_TRACE", "CLOSE_DETAIL id=%u r_short=%u r_err=%u w_short=%u w_err=%u seek_calls=%u seek_err=%u sync_calls=%u sync_err=%u flush_calls=%u",
            traceId, readShort, readErrors, writeShort, writeErrors, seekCalls, seekErrors,
            syncCalls, syncErrors, flushCalls);
    LOG_INF("SD_TRACE", "CLOSE_TIME id=%u r_us=%llu w_us=%llu seek_us=%llu sync_us=%llu",
            traceId, static_cast<unsigned long long>(readUs), static_cast<unsigned long long>(writeUs),
            static_cast<unsigned long long>(seekUs), static_cast<unsigned long long>(syncUs));
  }
#endif
};

HalFile::HalFile() = default;
HalFile::HalFile(std::unique_ptr<Impl> impl) : impl(std::move(impl)) {}
HalFile::~HalFile() = default;
HalFile::HalFile(HalFile&&) = default;
HalFile& HalFile::operator=(HalFile&&) = default;

HalFile HalStorage::open(const char* path, const oflag_t oflag) {
  StorageLock lock;  // ensure thread safety for the duration of this function
#ifdef TENOR_UI_ACCEPTANCE
  FsFile fsFile = SDCard.open(path, oflag);
  const bool opened = fsFile.isOpen();
  return HalFile(std::make_unique<HalFile::Impl>(std::move(fsFile), "HalStorage", path, "raw", opened,
                                                 static_cast<unsigned>(oflag)));
#else
  return HalFile(std::make_unique<HalFile::Impl>(SDCard.open(path, oflag)));
#endif
}

bool HalStorage::mkdir(const char* path, const bool pFlag) { HAL_STORAGE_WRAPPED_CALL(mkdir, path, pFlag); }

bool HalStorage::exists(const char* path) { HAL_STORAGE_WRAPPED_CALL(exists, path); }

bool HalStorage::remove(const char* path) { HAL_STORAGE_WRAPPED_CALL(remove, path); }
bool HalStorage::rename(const char* oldPath, const char* newPath) {
  HAL_STORAGE_WRAPPED_CALL(rename, oldPath, newPath);
}

bool HalStorage::rmdir(const char* path) { HAL_STORAGE_WRAPPED_CALL(rmdir, path); }

bool HalStorage::openFileForRead(const char* moduleName, const char* path, HalFile& file) {
  StorageLock lock;  // ensure thread safety for the duration of this function
  FsFile fsFile;
  bool ok = SDCard.openFileForRead(moduleName, path, fsFile);
#ifdef TENOR_UI_ACCEPTANCE
  file = HalFile(std::make_unique<HalFile::Impl>(std::move(fsFile), moduleName, path, "read", ok));
#else
  file = HalFile(std::make_unique<HalFile::Impl>(std::move(fsFile)));
#endif
  return ok;
}

bool HalStorage::openFileForRead(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForRead(const char* moduleName, const String& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const char* path, HalFile& file) {
  StorageLock lock;  // ensure thread safety for the duration of this function
  FsFile fsFile;
  bool ok = SDCard.openFileForWrite(moduleName, path, fsFile);
#ifdef TENOR_UI_ACCEPTANCE
  file = HalFile(std::make_unique<HalFile::Impl>(std::move(fsFile), moduleName, path, "write", ok));
#else
  file = HalFile(std::make_unique<HalFile::Impl>(std::move(fsFile)));
#endif
  return ok;
}

bool HalStorage::openFileForWrite(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const String& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::removeDir(const char* path) { HAL_STORAGE_WRAPPED_CALL(removeDir, path); }

// HalFile implementation
// Allow doing file operations while ensuring thread safety via HalStorage's mutex.
// Please keep the list below in sync with the HalFile.h header

#define HAL_FILE_WRAPPED_CALL(method, ...) \
  HalStorage::StorageLock lock;            \
  assert(impl != nullptr);                 \
  return impl->file.method(__VA_ARGS__);

#define HAL_FILE_FORWARD_CALL(method, ...) \
  assert(impl != nullptr);                 \
  return impl->file.method(__VA_ARGS__);

void HalFile::flush() {
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  impl->file.flush();
#ifdef TENOR_UI_ACCEPTANCE
  ++impl->flushCalls;
#endif
}
bool HalFile::sync() {
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
#ifdef TENOR_UI_ACCEPTANCE
  const int64_t started = esp_timer_get_time();
#endif
  const bool ok = impl->file.sync();
#ifdef TENOR_UI_ACCEPTANCE
  impl->recordSync(ok, started);
#endif
  return ok;
}
size_t HalFile::getName(char* name, size_t len) { HAL_FILE_WRAPPED_CALL(getName, name, len); }
size_t HalFile::size() { HAL_FILE_FORWARD_CALL(size, ); }              // already thread-safe, no need to wrap
size_t HalFile::fileSize() { HAL_FILE_FORWARD_CALL(fileSize, ); }      // already thread-safe, no need to wrap
uint64_t HalFile::fileSize64() { HAL_FILE_FORWARD_CALL(fileSize, ); }  // already thread-safe, no need to wrap
#ifdef TENOR_UI_ACCEPTANCE
#define HAL_FILE_TRACED_SEEK(method, position)  \
  HalStorage::StorageLock lock;               \
  assert(impl != nullptr);                    \
  const int64_t started = esp_timer_get_time(); \
  const bool ok = impl->file.method(position); \
  impl->recordSeek(ok, started);              \
  return ok;
bool HalFile::seek(size_t pos) { HAL_FILE_TRACED_SEEK(seekSet, pos); }
bool HalFile::seek64(uint64_t pos) { HAL_FILE_TRACED_SEEK(seekSet, pos); }
bool HalFile::seekCur(int64_t offset) { HAL_FILE_TRACED_SEEK(seekCur, offset); }
bool HalFile::seekSet(size_t offset) { HAL_FILE_TRACED_SEEK(seekSet, offset); }
#else
bool HalFile::seek(size_t pos) { HAL_FILE_WRAPPED_CALL(seekSet, pos); }
bool HalFile::seek64(uint64_t pos) { HAL_FILE_WRAPPED_CALL(seekSet, pos); }
bool HalFile::seekCur(int64_t offset) { HAL_FILE_WRAPPED_CALL(seekCur, offset); }
bool HalFile::seekSet(size_t offset) { HAL_FILE_WRAPPED_CALL(seekSet, offset); }
#endif
#ifdef TENOR_UI_ACCEPTANCE
#undef HAL_FILE_TRACED_SEEK
#endif
int HalFile::available() const { HAL_FILE_WRAPPED_CALL(available, ); }
size_t HalFile::position() const { HAL_FILE_WRAPPED_CALL(position, ); }
#ifdef TENOR_UI_ACCEPTANCE
#define HAL_FILE_TRACED_READ(method, requested, ...) \
  HalStorage::StorageLock lock;                      \
  assert(impl != nullptr);                           \
  const int64_t started = esp_timer_get_time();      \
  const int result = impl->file.method(__VA_ARGS__);  \
  impl->recordRead(result, requested, started);      \
  return result;
#define HAL_FILE_TRACED_WRITE(requested, ...)         \
  HalStorage::StorageLock lock;                       \
  assert(impl != nullptr);                            \
  const int64_t started = esp_timer_get_time();       \
  const size_t result = impl->file.write(__VA_ARGS__); \
  impl->recordWrite(result, requested, started);      \
  return result;
int HalFile::read(void* buf, size_t count) { HAL_FILE_TRACED_READ(read, count, buf, count); }
int HalFile::read() {
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  const int64_t started = esp_timer_get_time();
  const int result = impl->file.read();
  impl->recordRead(result < 0 ? 0 : 1, 1, started);  // -1 is the single-byte EOF sentinel.
  return result;
}
size_t HalFile::write(const uint8_t* buf, size_t count) { HAL_FILE_TRACED_WRITE(count, buf, count); }
size_t HalFile::write(const void* buf, size_t count) { HAL_FILE_TRACED_WRITE(count, buf, count); }
size_t HalFile::write(uint8_t b) { HAL_FILE_TRACED_WRITE(1, b); }
#undef HAL_FILE_TRACED_READ
#undef HAL_FILE_TRACED_WRITE
#else
int HalFile::read(void* buf, size_t count) { HAL_FILE_WRAPPED_CALL(read, buf, count); }
int HalFile::read() { HAL_FILE_WRAPPED_CALL(read, ); }
size_t HalFile::write(const uint8_t* buf, size_t count) { HAL_FILE_WRAPPED_CALL(write, buf, count); }
size_t HalFile::write(const void* buf, size_t count) { HAL_FILE_WRAPPED_CALL(write, buf, count); }
size_t HalFile::write(uint8_t b) { HAL_FILE_WRAPPED_CALL(write, b); }
#endif
bool HalFile::rename(const char* newPath) { HAL_FILE_WRAPPED_CALL(rename, newPath); }
bool HalFile::isDirectory() const { HAL_FILE_FORWARD_CALL(isDirectory, ); }  // already thread-safe, no need to wrap
void HalFile::rewindDirectory() { HAL_FILE_WRAPPED_CALL(rewindDirectory, ); }
bool HalFile::close() {
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  const bool ok = impl->file.close();
#ifdef TENOR_UI_ACCEPTANCE
  impl->traceClose("explicit", ok);
#endif
  return ok;
}
HalFile HalFile::openNextFile() {
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
#ifdef TENOR_UI_ACCEPTANCE
  FsFile fsFile = impl->file.openNextFile();
  const bool opened = fsFile.isOpen();
  char name[128] = {};
  if (opened) fsFile.getName(name, sizeof(name));
  return HalFile(std::make_unique<Impl>(std::move(fsFile), "HalFile", opened ? name : "", "next",
                                             opened, 0, impl->traceId));
#else
  return HalFile(std::make_unique<Impl>(impl->file.openNextFile()));
#endif
}
bool HalFile::isOpen() const { return impl != nullptr && impl->file.isOpen(); }  // already thread-safe, no need to wrap
HalFile::operator bool() const { return isOpen(); }
