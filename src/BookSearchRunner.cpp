#include "BookSearchRunner.h"

#ifdef BOARD_HAS_PSRAM
#include <Epub.h>
#include <HalMemory.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HeapFloor.h>
#include <Print.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>

namespace {
struct SearchState {
  booksearch::Workspace work;
  booksearch::Results results;
  char query[booksearch::kMaxQueryCodepoints * 4 + 1];
};

void* psram(const size_t bytes) {
  auto block = HalMemory::allocatePsram(bytes);
  if (!block || !heapfloor::judge(bytes, block.get(), "book-search")) return nullptr;
  return block.release();
}
void release(void* block) { HalMemory::PsramDeleter{}(static_cast<uint8_t*>(block)); }

struct alignas(std::max_align_t) XmlHeader { size_t bytes; };
void* xmlAllocate(const size_t bytes) {
  if (bytes > std::numeric_limits<size_t>::max() - sizeof(XmlHeader)) return nullptr;
  auto* header = static_cast<XmlHeader*>(psram(bytes + sizeof(XmlHeader)));
  if (!header) return nullptr;
  header->bytes = bytes;
  return header + 1;
}
void xmlFree(void* block) {
  if (block) release(static_cast<XmlHeader*>(block) - 1);
}
void* xmlResize(void* block, const size_t bytes) {
  if (!block) return xmlAllocate(bytes);
  if (!bytes) { xmlFree(block); return nullptr; }
  void* next = xmlAllocate(bytes);
  if (!next) return nullptr;
  std::memcpy(next, block, std::min(bytes, (static_cast<XmlHeader*>(block) - 1)->bytes));
  xmlFree(block);
  return next;
}
const XML_Memory_Handling_Suite xmlMemory{xmlAllocate, xmlResize, xmlFree};

class Sink : public Print {
 public:
  Sink(booksearch::Scanner& scanner, booksearch::Results& results, BookSearchRunner::Stop stop,
       void* context, size_t& bytes)
      : scanner(scanner), matches(results), stop(stop), context(context), bytes(bytes) {}
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* data, const size_t length) override {
    if (matches.full()) return 0;
    if (stop && stop(context)) { cancelled = true; return 0; }
    if (!scanner.feed(reinterpret_cast<const char*>(data), length)) return 0;
    bytes += length;
    return matches.full() ? 0 : length;
  }
  booksearch::Scanner& scanner;
  booksearch::Results& matches;
  BookSearchRunner::Stop stop;
  void* context;
  size_t& bytes;
  bool cancelled = false;
};
}  // namespace
#endif

BookSearchRunner::~BookSearchRunner() { clear(); }
void BookSearchRunner::clear() {
#ifdef BOARD_HAS_PSRAM
  if (storage_) {
    static_cast<SearchState*>(storage_)->~SearchState();
    release(storage_);
  }
#endif
  storage_ = nullptr;
  bytes_ = 0;
  status_ = supported() ? Status::Ready : Status::Unsupported;
}

BookSearchRunner::Status BookSearchRunner::begin([[maybe_unused]] const char* query) {
  clear();
#ifdef BOARD_HAS_PSRAM
  if (!query || std::strlen(query) > sizeof(SearchState::query) - 1) return status_ = Status::InvalidQuery;
  void* memory = psram(sizeof(SearchState));
  if (!memory) return status_ = Status::NoMemory;
  auto* storage = new (memory) SearchState{};
  storage_ = storage;
  std::strcpy(storage->query, query);
  booksearch::Scanner scanner(storage->work, storage->results, &xmlMemory);
  const auto result = scanner.begin(storage->query, 0);
  if (result != booksearch::Status::Ready) {
    const Status failure = result == booksearch::Status::NoMemory ? Status::NoMemory : Status::InvalidQuery;
    clear();
    return status_ = failure;
  }
  return status_ = Status::Ready;
#else
  return status_ = Status::Unsupported;
#endif
}

const booksearch::Results* BookSearchRunner::results() const {
#ifdef BOARD_HAS_PSRAM
  return storage_ ? &static_cast<const SearchState*>(storage_)->results : nullptr;
#else
  return nullptr;
#endif
}

BookSearchRunner::Status BookSearchRunner::scanChapter([[maybe_unused]] const Epub& epub,
    [[maybe_unused]] const int spine, [[maybe_unused]] const Stop stop, [[maybe_unused]] void* context) {
#ifdef BOARD_HAS_PSRAM
  if (!storage_) return status_ = Status::InvalidQuery;
  auto& storage = *static_cast<SearchState*>(storage_);
  if (storage.results.full()) return status_ = Status::Full;
  if (spine < 0 || spine >= epub.getSpineItemsCount() || spine > UINT16_MAX) return status_ = Status::InvalidSpine;
  if (stop && stop(context)) return status_ = Status::Cancelled;
  const size_t before = storage.results.count;
  const uint32_t refusals = heapfloor::refusals.load();
  [[maybe_unused]] HalPowerManager::Lock power;
  booksearch::Scanner scanner(storage.work, storage.results, &xmlMemory);
  const auto started = scanner.begin(storage.query, static_cast<uint16_t>(spine));
  if (started != booksearch::Status::Ready) return status_ = Status::NoMemory;
  Sink sink(scanner, storage.results, stop, context, bytes_);
  const std::string path = epub.getCachePath() + "/html/" + std::to_string(spine) + ".html";
  bool read = false;
  if (Storage.exists(path.c_str())) {
    HalFile file;
    if (Storage.openFileForRead("BKS", path, file)) {
      uint8_t buffer[1024];
      size_t remaining = file.size();
      read = true;
      while (remaining && !storage.results.full()) {
        if (stop && stop(context)) { sink.cancelled = true; break; }
        const size_t wanted = std::min(remaining, sizeof(buffer));
        const int received = file.read(buffer, wanted);
        if (received <= 0 || static_cast<size_t>(received) > wanted) { read = false; break; }
        remaining -= static_cast<size_t>(received);
        if (sink.write(buffer, static_cast<size_t>(received)) != static_cast<size_t>(received)) {
          read = storage.results.full();
          break;
        }
      }
    }
  } else {
    read = epub.readItemContentsToStream(epub.getSpineItem(spine).href, sink, 1024, true);
  }
  const bool finished = !sink.cancelled && read && scanner.finish();
  if (sink.cancelled) status_ = Status::Cancelled;
  else if (scanner.status() == booksearch::Status::NoMemory || heapfloor::refusals.load() != refusals)
    status_ = Status::NoMemory;
  else if (scanner.status() == booksearch::Status::ParseError) status_ = Status::ParseError;
  else if (!finished) status_ = Status::ReadError;
  else status_ = storage.results.full() ? Status::Full : Status::Complete;
  if (status_ != Status::Complete && status_ != Status::Full) storage.results.count = before;
  return status_;
#else
  return status_ = Status::Unsupported;
#endif
}
