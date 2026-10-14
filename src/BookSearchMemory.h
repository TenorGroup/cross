#pragma once

#include <string>

#include "BookSearchRunner.h"

// What a book's text search leaves in RAM between visits to its screens: the reader keeps it until the book closes,
// and "Search in book" opens on the last results with the query to change.
struct BookSearchMemory {
  BookSearchRunner runner;
  std::string query;
  // The next chapter to scan, and how many the search covers.
  int nextSpine = 0;
  int chapters = 0;
  // Stopped by the reader, or a chapter that could not be read: the results are not the whole book's.
  bool incomplete = false;
  // The search itself failed to start or went on (BookSearchRunner::Status::NoMemory).
  bool failed = false;

  bool hasResults() const { return !query.empty() && runner.results(); }
};
