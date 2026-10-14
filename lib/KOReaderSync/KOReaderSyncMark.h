#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// What this machine last put on the server or took from it for a book: the server's record (a number
// from kosyncauto::recordToken) and this machine's place then (kosyncauto::placeKey). The wake pull and
// the sleep push compare the server's record with it to tell "another machine wrote" from "this
// machine's own older record". The book is named by its document hash, the same key the server uses.
struct SyncMark {
  std::string document;
  uint32_t token = 0;
  uint32_t place = 0;
};

// At most kosyncauto::MAX_MARKS, the oldest first; /.crosspoint/kosync-last.json, written through the
// checked staging replacement of PersistableStoreBase like the debt list.
class SyncMarkList {
 public:
  // Adds the book, or replaces its earlier mark and makes it the newest.
  void put(SyncMark mark);
  // nullptr when the book has no mark.
  const SyncMark* find(const std::string& document) const;
  // The server still holds the record this machine last put there or took from there (no other machine
  // wrote since). False for a book without a mark.
  bool serverIsOurs(const std::string& document, const std::string& progress, float percentage) const;
  // This machine's place moved since that record (true for a book without a mark).
  bool placeChanged(const std::string& document, uint32_t place) const;
  size_t size() const { return items_.size(); }
  bool empty() const { return items_.empty(); }

  void toJson(JsonDocument& doc) const;
  // Entries that do not parse are skipped; false when the document is not the expected shape (the list is
  // then empty).
  bool fromJson(JsonVariantConst doc);

 private:
  std::vector<SyncMark> items_;
};
