#include "util/DictionaryLookupPolicy.h"

#include <Utf8.h>

#include <utility>

namespace DictionaryLookupPolicy {
namespace {

bool isWhitespace(const uint32_t cp) {
  switch (cp) {
    case 0x0009:
    case 0x000A:
    case 0x000B:
    case 0x000C:
    case 0x000D:
    case 0x0020:
    case 0x00A0:
    case 0x1680:
    case 0x2028:
    case 0x2029:
    case 0x202F:
    case 0x205F:
    case 0x3000:
      return true;
    default:
      return (cp >= 0x2000 && cp <= 0x200A);
  }
}

uint32_t lowerLatin(const uint32_t cp) {
  if (cp >= 'A' && cp <= 'Z') return cp + ('a' - 'A');

  // Latin-1 uppercase letters, excluding multiplication sign.
  if ((cp >= 0x00C0 && cp <= 0x00D6) || (cp >= 0x00D8 && cp <= 0x00DE)) return cp + 0x20;

  // Latin Extended-A pairs used by Vietnamese and neighbouring Latin scripts.
  if (cp == 0x0102 || cp == 0x0110 || cp == 0x0128 || cp == 0x0168 || cp == 0x01A0 || cp == 0x01AF) return cp + 1;

  // Vietnamese precomposed letters are arranged as uppercase/lowercase pairs.
  if (cp >= 0x1EA0 && cp <= 0x1EF9 && (cp & 1u) == 0) return cp + 1;

  return cp;
}

std::vector<std::string> splitTokens(const std::string& text) {
  std::vector<std::string> tokens;
  const auto* p = reinterpret_cast<const unsigned char*>(text.c_str());
  std::string token;
  while (*p) {
    const auto* start = p;
    const uint32_t cp = utf8NextCodepoint(&p);
    if (isWhitespace(cp)) {
      if (!token.empty()) {
        tokens.push_back(std::move(token));
        token.clear();
      }
      continue;
    }
    token.append(reinterpret_cast<const char*>(start), static_cast<size_t>(p - start));
  }
  if (!token.empty()) tokens.push_back(std::move(token));
  return tokens;
}

}  // namespace

std::vector<std::string> phraseCandidates(const std::string& text) {
  const std::vector<std::string> tokens = splitTokens(text);
  const size_t count = tokens.size() < 3 ? tokens.size() : 3;
  std::vector<std::string> candidates;
  candidates.reserve(count);
  for (size_t length = count; length > 0; --length) {
    std::string candidate = tokens[0];
    for (size_t i = 1; i < length; ++i) {
      candidate.push_back(' ');
      candidate += tokens[i];
    }
    candidates.push_back(std::move(candidate));
  }
  return candidates;
}

std::string comparatorKey(const std::string& text) {
  const std::string nfc = utf8ComposeNfc(text);
  std::string key;
  key.reserve(nfc.size());
  const auto* p = reinterpret_cast<const unsigned char*>(nfc.c_str());
  while (*p) utf8AppendCodepoint(lowerLatin(utf8NextCodepoint(&p)), key);
  return key;
}

bool hasIndexOrderWarning(const std::string& previous, const std::string& current) {
  return comparatorKey(current) < comparatorKey(previous);
}

bool hasIndexOrderWarning(const std::vector<std::string>& headwords) {
  if (headwords.size() < 2) return false;
  for (size_t i = 1; i < headwords.size(); ++i) {
    if (hasIndexOrderWarning(headwords[i - 1], headwords[i])) return true;
  }
  return false;
}

bool useLinearFallback(const bool indexOrderWarning) { return indexOrderWarning; }

}  // namespace DictionaryLookupPolicy
