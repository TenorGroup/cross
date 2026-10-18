#pragma once

#include <string>
#include <vector>

namespace DictionaryLookupPolicy {

// Return the first 1 to 3 whitespace-separated tokens, longest first.
// Tokens are kept as UTF-8 text and joined with 1 ASCII space.
std::vector<std::string> phraseCandidates(const std::string& text);

// Return the key used for dictionary ordering and exact comparison. The key
// is NFC-composed and lowercased while retaining Vietnamese diacritics.
std::string comparatorKey(const std::string& text);

// Return true when adjacent index headwords are out of comparator order.
bool hasIndexOrderWarning(const std::string& previous, const std::string& current);
bool hasIndexOrderWarning(const std::vector<std::string>& headwords);

// An index with an order warning must use a linear scan instead of sampled
// binary lookup.
bool useLinearFallback(bool indexOrderWarning);

}  // namespace DictionaryLookupPolicy
