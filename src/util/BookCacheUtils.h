#pragma once

#include <string>

// Clears the reading cache for a book file if its extension is recognised
// (EPUB, XTC, or TXT). Other types return true. False means stale cache
// may remain and callers must report cleanup failure after a file commit.
bool clearBookCache(const std::string& path);

// Returns true if the directory name matches a book cache entry.
bool isBookCacheDirectoryName(const char* name);
