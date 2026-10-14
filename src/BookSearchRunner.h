#pragma once

#include <BookSearch.h>

#include <cstddef>
#include <cstdint>

class Epub;

class BookSearchRunner {
 public:
  enum class Status : uint8_t {
    Ready, Complete, Full, Cancelled, Unsupported, InvalidQuery, InvalidSpine, NoMemory, ReadError, ParseError
  };
  using Stop = bool (*)(void*);
  BookSearchRunner() = default;
  ~BookSearchRunner();
  BookSearchRunner(const BookSearchRunner&) = delete;
  BookSearchRunner& operator=(const BookSearchRunner&) = delete;
  static constexpr bool supported() {
#ifdef BOARD_HAS_PSRAM
    return true;
#else
    return false;
#endif
  }
  Status begin(const char* query);
  // Call synchronously for each spine, under the reader's render lock.
  // Cancel/error discards only this chapter's partial matches, so it can be retried.
  Status scanChapter(const Epub& epub, int spine, Stop stop = nullptr, void* context = nullptr);
  void clear();
  const booksearch::Results* results() const;
  Status status() const { return status_; }
  size_t bytesScanned() const { return bytes_; }

 private:
  void* storage_ = nullptr;
  Status status_ = Status::Unsupported;
  size_t bytes_ = 0;
};
