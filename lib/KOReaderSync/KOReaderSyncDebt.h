#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// A book left since the last automatic push. Cheap to write when the reader closes: the KOReader
// XPath is worked out later, at sleep, from this position.
struct SyncDebt {
  std::string path;
  uint16_t spineIndex = 0;
  uint16_t pageNumber = 0;
  uint16_t totalPages = 0;
  uint32_t visibleTextOffset = 0;
  bool hasVisibleTextOffset = false;
  uint16_t paragraphIndex = 0;
  bool hasParagraphIndex = false;
  float percentage = 0.0f;  // 0..1, the value the server is compared against
  uint32_t leftAt = 0;      // unix seconds, 0 when the clock was not set
};

// The books owed a push, oldest first. At most kosyncauto::MAX_DEBTS; the file is
// /.crosspoint/kosync-no.json, read and written by the firmware side through the checked
// staging replacement of PersistableStoreBase.
class SyncDebtList {
 public:
  // Adds the book, or replaces its earlier debt and makes it the newest.
  void put(SyncDebt debt);
  bool remove(const std::string& path);
  size_t size() const { return items_.size(); }
  bool empty() const { return items_.empty(); }
  const SyncDebt& at(size_t index) const { return items_[index]; }

  void toJson(JsonDocument& doc) const;
  // Rebuilds from a document. Entries that do not parse are skipped; false when the document is not
  // the expected shape (the list is then empty).
  bool fromJson(JsonVariantConst doc);

 private:
  std::vector<SyncDebt> items_;
};
