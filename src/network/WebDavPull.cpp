#include "WebDavPull.h"

#include <algorithm>
#include <cctype>
#include <limits>

namespace webdav {
namespace {

std::string lower(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  for (const unsigned char c : value) out.push_back(static_cast<char>(std::tolower(c)));
  return out;
}

bool foldedEqual(std::string_view a, std::string_view b) {
  return a.size() == b.size() && lower(a) == lower(b);
}

std::string_view localName(std::string_view name) {
  const std::size_t colon = name.rfind(':');
  return colon == std::string_view::npos ? name : name.substr(colon + 1);
}

bool validName(std::string_view name) {
  if (name.empty()) return false;
  for (const unsigned char c : name) {
    if (std::isalnum(c) || c == '_' || c == '-' || c == '.' || c == ':') continue;
    return false;
  }
  return true;
}

bool isWhitespace(std::string_view value) {
  return std::all_of(value.begin(), value.end(), [](const unsigned char c) { return std::isspace(c) != 0; });
}

bool decodeEntities(std::string_view input, std::string& output) {
  output.clear();
  output.reserve(input.size());
  for (std::size_t i = 0; i < input.size(); ++i) {
    if (input[i] != '&') {
      output.push_back(input[i]);
      continue;
    }
    const std::size_t semi = input.find(';', i + 1);
    if (semi == std::string_view::npos || semi - i > 10) return false;
    const std::string_view entity = input.substr(i + 1, semi - i - 1);
    if (entity == "amp") output.push_back('&');
    else if (entity == "lt") output.push_back('<');
    else if (entity == "gt") output.push_back('>');
    else if (entity == "quot") output.push_back('"');
    else if (entity == "apos") output.push_back('\'');
    else if (entity.size() >= 2 && entity[0] == '#') {
      unsigned value = 0;
      std::size_t start = 1;
      int base = 10;
      if (entity[start] == 'x' || entity[start] == 'X') {
        base = 16;
        ++start;
      }
      if (start == entity.size()) return false;
      for (; start < entity.size(); ++start) {
        const char c = entity[start];
        unsigned digit = 0;
        if (c >= '0' && c <= '9') digit = static_cast<unsigned>(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f') digit = static_cast<unsigned>(c - 'a' + 10);
        else if (base == 16 && c >= 'A' && c <= 'F') digit = static_cast<unsigned>(c - 'A' + 10);
        else return false;
        if (value > (0x10ffffu - digit) / static_cast<unsigned>(base)) return false;
        value = value * static_cast<unsigned>(base) + digit;
      }
      if (value == 0 || value > 0x10ffffu || (value >= 0xd800u && value <= 0xdfffu)) return false;
      if (value < 0x80) output.push_back(static_cast<char>(value));
      else if (value < 0x800) {
        output.push_back(static_cast<char>(0xc0 | (value >> 6)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
      } else if (value < 0x10000) {
        output.push_back(static_cast<char>(0xe0 | (value >> 12)));
        output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
      } else {
        output.push_back(static_cast<char>(0xf0 | (value >> 18)));
        output.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
      }
    } else {
      return false;
    }
    i = semi;
  }
  return true;
}

bool percentDecode(std::string_view input, std::string& output) {
  output.clear();
  output.reserve(input.size());
  for (std::size_t i = 0; i < input.size(); ++i) {
    if (input[i] != '%') {
      output.push_back(input[i]);
      continue;
    }
    if (i + 2 >= input.size()) return false;
    const auto hex = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    const int high = hex(input[i + 1]);
    const int low = hex(input[i + 2]);
    if (high < 0 || low < 0) return false;
    output.push_back(static_cast<char>((high << 4) | low));
    i += 2;
  }
  return true;
}

struct UrlOrigin {
  std::string scheme;
  std::string host;
  unsigned port = 0;
};

bool parseOrigin(std::string_view url, UrlOrigin& origin) {
  const std::size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string_view::npos || schemeEnd == 0) return false;
  origin.scheme = lower(url.substr(0, schemeEnd));
  if (origin.scheme != "http" && origin.scheme != "https") return false;
  const std::size_t authorityStart = schemeEnd + 3;
  const std::size_t authorityEnd = url.find_first_of("/?#", authorityStart);
  const std::string_view authority = url.substr(authorityStart, authorityEnd == std::string_view::npos
                                                            ? url.size() - authorityStart
                                                            : authorityEnd - authorityStart);
  if (authority.empty() || authority.find('@') != std::string_view::npos || authority.find('\\') != std::string_view::npos)
    return false;
  const std::size_t colon = authority.rfind(':');
  std::string_view host = authority;
  if (colon != std::string_view::npos) {
    host = authority.substr(0, colon);
    const std::string_view portText = authority.substr(colon + 1);
    if (host.empty() || portText.empty() || portText.size() > 5) return false;
    unsigned port = 0;
    for (const char c : portText) {
      if (c < '0' || c > '9') return false;
      port = port * 10u + static_cast<unsigned>(c - '0');
      if (port > 65535u) return false;
    }
    origin.port = port;
  } else {
    origin.port = origin.scheme == "https" ? 443u : 80u;
  }
  if (host.empty()) return false;
  origin.host = lower(host);
  for (const unsigned char c : origin.host) {
    if (std::isspace(c) || c < 32 || c == 127) return false;
  }
  return true;
}

bool sameOrigin(const UrlOrigin& a, const UrlOrigin& b) {
  return a.port == b.port && a.scheme == b.scheme && a.host == b.host;
}

ParseError pathFromHref(std::string_view href, std::string_view expectedOrigin, std::string& path, std::size_t& depth) {
  std::string decodedHref;
  if (!decodeEntities(href, decodedHref)) return ParseError::invalid_path;
  std::string_view pathView = decodedHref;
  const std::size_t scheme = decodedHref.find("://");
  if (scheme != std::string::npos) {
    UrlOrigin hrefOrigin;
    if (!parseOrigin(decodedHref, hrefOrigin) || expectedOrigin.empty()) return ParseError::unsafe_origin;
    UrlOrigin expected;
    if (!parseOrigin(expectedOrigin, expected) || !sameOrigin(hrefOrigin, expected)) return ParseError::unsafe_origin;
    const std::size_t slash = decodedHref.find('/', scheme + 3);
    pathView = slash == std::string::npos ? std::string_view("/") : std::string_view(decodedHref).substr(slash);
  } else if (decodedHref.rfind("//", 0) == 0) {
    return ParseError::unsafe_origin;
  }
  const std::size_t query = pathView.find_first_of("?#");
  if (query != std::string_view::npos) pathView = pathView.substr(0, query);
  if (pathView.empty() || pathView.front() != '/') return ParseError::invalid_path;
  if (!percentDecode(pathView, path)) return ParseError::invalid_path;
  if (path.size() > WebDavPullLimits::maxPath) return ParseError::invalid_path;
  depth = 0;
  std::size_t start = 1;
  for (std::size_t i = 1; i <= path.size(); ++i) {
    if (i < path.size() && path[i] != '/') continue;
    const std::string_view part(path.data() + start, i - start);
    if (part.empty()) {
      if (i < path.size()) return ParseError::invalid_path;
    } else {
      if (part == "." || part == "..") return ParseError::invalid_path;
      ++depth;
      if (depth > WebDavPullLimits::maxDepth) return ParseError::too_deep;
      for (const unsigned char c : part) {
        if (c < 32 || c == 127 || c == '\\') return ParseError::invalid_path;
      }
    }
    start = i + 1;
  }
  return ParseError::none;
}

bool parseUnsigned(std::string_view text, std::uint64_t& value) {
  value = 0;
  if (text.empty()) return false;
  for (const char c : text) {
    if (c < '0' || c > '9') return false;
    const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
    if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10u) return false;
    value = value * 10u + digit;
  }
  return true;
}

}  // namespace

ParseResult parsePropfind207(std::string_view xml, std::string_view expectedOrigin) {
  ParseResult result;

  struct Current {
    std::size_t start = 0;
    std::string href;
    std::string contentType;
    std::string contentLength;
    std::string xmlTextField;
    std::size_t xmlText = 0;
    std::size_t propertyText = 0;
    bool collection = false;
  };
  std::vector<std::string> stack;
  Current current;
  bool inResponse = false;
  bool sawRoot = false;
  std::size_t pos = 0;
  auto noteText = [&](std::string_view field, std::string_view text) {
    if (current.xmlTextField != field) {
      current.xmlTextField.assign(field);
      current.xmlText = 0;
    }
    current.xmlText += text.size();
    if (current.xmlText > WebDavPullLimits::maxXmlText) {
      result.error = ParseError::oversized_xml;
      return false;
    }
    return true;
  };
  while (pos < xml.size()) {
    const std::size_t open = xml.find('<', pos);
    if (open == std::string_view::npos) {
      const std::string_view text = xml.substr(pos);
      if (!inResponse && !isWhitespace(text)) {
        result.error = ParseError::malformed_xml;
        return result;
      }
      if (inResponse && !isWhitespace(text)) {
        const std::string_view field = localName(stack.empty() ? std::string_view{} : stack.back());
        if (!noteText(field, text)) return result;
        if (field == "href") current.href.append(text);
        else if (field == "getcontenttype") current.contentType.append(text);
        else if (field == "getcontentlength") current.contentLength.append(text);
        else current.propertyText += text.size();
        if (current.href.size() > WebDavPullLimits::maxHref) result.error = ParseError::oversized_href;
        if (current.propertyText > WebDavPullLimits::maxProperty ||
            current.contentType.size() > WebDavPullLimits::maxProperty || current.contentLength.size() > WebDavPullLimits::maxProperty)
          result.error = ParseError::oversized_property;
      }
      break;
    }
    if (open > pos && !inResponse && !isWhitespace(xml.substr(pos, open - pos))) {
      result.error = ParseError::malformed_xml;
      return result;
    }
    if (open > pos && inResponse && !isWhitespace(xml.substr(pos, open - pos))) {
      const std::string_view text = xml.substr(pos, open - pos);
      const std::string_view field = localName(stack.empty() ? std::string_view{} : stack.back());
      if (!noteText(field, text)) return result;
      if (field == "href") current.href.append(text);
      else if (field == "getcontenttype") current.contentType.append(text);
      else if (field == "getcontentlength") current.contentLength.append(text);
      else current.propertyText += text.size();
      if (current.href.size() > WebDavPullLimits::maxHref) result.error = ParseError::oversized_href;
      if (current.propertyText > WebDavPullLimits::maxProperty ||
          current.contentType.size() > WebDavPullLimits::maxProperty || current.contentLength.size() > WebDavPullLimits::maxProperty)
        result.error = ParseError::oversized_property;
    }
    if (result.error != ParseError::none) return result;

    if (xml.compare(open, 4, "<!--") == 0) {
      const std::size_t end = xml.find("-->", open + 4);
      if (end == std::string_view::npos) { result.error = ParseError::malformed_xml; return result; }
      pos = end + 3;
      continue;
    }
    if (xml.compare(open, 9, "<![CDATA[") == 0) {
      const std::size_t end = xml.find("]]>", open + 9);
      if (end == std::string_view::npos) { result.error = ParseError::malformed_xml; return result; }
      if (inResponse) {
        const std::string_view text = xml.substr(open + 9, end - open - 9);
        const std::string_view field = localName(stack.empty() ? std::string_view{} : stack.back());
        if (!noteText(field, text)) return result;
        if (field == "href") current.href.append(text);
        else if (field == "getcontenttype") current.contentType.append(text);
        else if (field == "getcontentlength") current.contentLength.append(text);
        else current.propertyText += text.size();
        if (current.propertyText > WebDavPullLimits::maxProperty || current.href.size() > WebDavPullLimits::maxHref ||
            current.contentType.size() > WebDavPullLimits::maxProperty || current.contentLength.size() > WebDavPullLimits::maxProperty) {
          result.error = current.href.size() > WebDavPullLimits::maxHref ? ParseError::oversized_href
                                                                           : ParseError::oversized_property;
          return result;
        }
      }
      pos = end + 3;
      continue;
    }
    if (xml[open + 1] == '?' || xml[open + 1] == '!') {
      const std::size_t end = xml.find('>', open + 2);
      if (end == std::string_view::npos) { result.error = ParseError::malformed_xml; return result; }
      if (xml[open + 1] == '!') { result.error = ParseError::malformed_xml; return result; }
      pos = end + 1;
      continue;
    }
    const std::size_t end = xml.find('>', open + 1);
    if (end == std::string_view::npos) { result.error = ParseError::malformed_xml; return result; }
    std::string_view token = xml.substr(open + 1, end - open - 1);
    bool closing = false;
    if (!token.empty() && token.front() == '/') {
      closing = true;
      token.remove_prefix(1);
    }
    while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back()))) token.remove_suffix(1);
    bool selfClosing = false;
    if (!closing && !token.empty() && token.back() == '/') {
      selfClosing = true;
      token.remove_suffix(1);
      while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back()))) token.remove_suffix(1);
    }
    const std::size_t nameEnd = token.find_first_of(" \t\r\n");
    const std::string_view name = token.substr(0, nameEnd == std::string_view::npos ? token.size() : nameEnd);
    if (!validName(name)) { result.error = ParseError::malformed_xml; return result; }
    if (!closing && nameEnd != std::string_view::npos) {
      std::string_view attrs = token.substr(nameEnd + 1);
      while (!attrs.empty()) {
        while (!attrs.empty() && std::isspace(static_cast<unsigned char>(attrs.front()))) attrs.remove_prefix(1);
        if (attrs.empty()) break;
        const std::size_t eq = attrs.find('=');
        if (eq == std::string_view::npos || eq == 0) { result.error = ParseError::malformed_xml; return result; }
        attrs.remove_prefix(eq + 1);
        if (attrs.empty() || (attrs.front() != '\'' && attrs.front() != '"')) { result.error = ParseError::malformed_xml; return result; }
        const char quote = attrs.front();
        attrs.remove_prefix(1);
        const std::size_t closeQuote = attrs.find(quote);
        if (closeQuote == std::string_view::npos) { result.error = ParseError::malformed_xml; return result; }
        attrs.remove_prefix(closeQuote + 1);
      }
    }
    const std::string local = lower(localName(name));
    if (closing) {
      if (stack.empty() || !foldedEqual(stack.back(), name)) { result.error = ParseError::malformed_xml; return result; }
      if (local == "response") {
        if (!inResponse || current.href.empty()) { result.error = ParseError::malformed_xml; return result; }
        if (end + 1 - current.start > WebDavPullLimits::maxEntry) { result.error = ParseError::oversized_entry; return result; }
        PropfindEntry entry;
        entry.href = current.href;
        entry.contentType = current.contentType;
        if (!current.contentLength.empty()) {
          if (!parseUnsigned(current.contentLength, entry.contentLength)) { result.error = ParseError::malformed_xml; return result; }
        }
        entry.collection = current.collection;
        const ParseError pathError = pathFromHref(entry.href, expectedOrigin, entry.path, entry.depth);
        if (pathError != ParseError::none) { result.error = pathError; return result; }
        result.entries.push_back(std::move(entry));
        current = Current{};
        inResponse = false;
      }
      stack.pop_back();
    } else {
      if (local == "multistatus" && stack.empty()) sawRoot = true;
      if (local == "response") {
        if (inResponse) { result.error = ParseError::malformed_xml; return result; }
        inResponse = true;
        current = Current{};
        current.start = open;
      }
      if (inResponse && local == "collection") current.collection = true;
      if (!selfClosing) {
        if (stack.size() >= 64) { result.error = ParseError::malformed_xml; return result; }
        stack.emplace_back(name);
      }
    }
    pos = end + 1;
  }
  if (!sawRoot || inResponse || !stack.empty()) result.error = ParseError::malformed_xml;
  return result;
}

ParseResult parsePropfind(std::string_view xml, std::string_view expectedOrigin) {
  return parsePropfind207(xml, expectedOrigin);
}

PropfindStreamParser::PropfindStreamParser(const std::string_view expectedOrigin)
    : expectedOrigin_(expectedOrigin) {
  pending_.reserve(WebDavPullLimits::maxEntry + 128u);
}

bool PropfindStreamParser::consume(const PropfindEntrySink sink, void* const context) {
  if (error_ != ParseError::none || sinkAborted_) return false;
  auto tagAt = [this](const std::size_t start, std::size_t& end, bool& closing, std::string& name) {
    if (start >= pending_.size() || pending_[start] != '<') return false;
    end = pending_.find('>', start + 1);
    if (end == std::string::npos) return false;
    std::string_view token(pending_.data() + start + 1, end - start - 1);
    closing = false;
    if (!token.empty() && token.front() == '/') {
      closing = true;
      token.remove_prefix(1);
    }
    while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back()))) token.remove_suffix(1);
    if (!token.empty() && token.back() == '/') token.remove_suffix(1);
    while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back()))) token.remove_suffix(1);
    if (!token.empty() && (token.front() == '?' || token.front() == '!')) {
      name.assign(1, token.front());
      return true;
    }
    const std::size_t nameEnd = token.find_first_of(" \t\r\n");
    name.assign(token.substr(0, nameEnd == std::string_view::npos ? token.size() : nameEnd));
    return !name.empty() && validName(name);
  };
  auto findTag = [&](const std::size_t from, const bool wantClosing, const char* wanted, std::size_t& tagEnd,
                     std::size_t& tagStart) {
    std::size_t cursor = from;
    while ((tagStart = pending_.find('<', cursor)) != std::string::npos) {
      bool closing = false;
      std::string name;
      if (!tagAt(tagStart, tagEnd, closing, name)) {
        if (pending_.find('>', tagStart + 1) == std::string::npos) return false;
        error_ = ParseError::malformed_xml;
        return false;
      }
      if (localName(name) == wanted && closing == wantClosing) return true;
      cursor = tagEnd + 1;
    }
    return false;
  };

  while (true) {
    std::size_t responseStart = std::string::npos;
    std::size_t responseTagEnd = std::string::npos;
    if (!inResponse_) {
      std::size_t cursor = 0;
      while (cursor < pending_.size()) {
        std::size_t tagEnd = std::string::npos;
        bool closing = false;
        std::string name;
        if (pending_[cursor] != '<') {
          ++cursor;
          continue;
        }
        if (!tagAt(cursor, tagEnd, closing, name)) {
          if (pending_.find('>', cursor + 1) == std::string::npos) break;
          error_ = ParseError::malformed_xml;
          return false;
        }
        const std::string local = std::string(localName(name));
        if (!closing && local == "multistatus") {
          sawRoot_ = true;
        } else if (closing && local == "multistatus") {
          sawRootClose_ = true;
        } else if (!closing && local == "response") {
          responseStart = cursor;
          responseTagEnd = tagEnd;
          break;
        } else if (!closing && (local == "!" || local == "?")) {
          // Declarations are consumed by the bounded full parser below.
        }
        cursor = tagEnd + 1;
      }
      if (responseStart == std::string::npos) {
        if (pending_.size() > WebDavPullLimits::maxXmlText + 128u) {
          error_ = ParseError::oversized_xml;
          return false;
        }
        // Keep a short suffix so a tag split across network chunks is retained.
        if (pending_.size() > 128u) pending_.erase(0, pending_.size() - 128u);
        return true;
      }
      if (!sawRoot_ || responseStart > 0) pending_.erase(0, responseStart);
      inResponse_ = true;
      (void)responseTagEnd;
    }

    std::size_t closeStart = std::string::npos;
    std::size_t closeEnd = std::string::npos;
    if (!findTag(0, true, "response", closeEnd, closeStart)) {
      if (error_ != ParseError::none) return false;
      if (pending_.size() > WebDavPullLimits::maxEntry) {
        error_ = ParseError::oversized_entry;
        return false;
      }
      return true;
    }
    if (closeEnd + 1 > WebDavPullLimits::maxEntry) {
      error_ = ParseError::oversized_entry;
      return false;
    }
    const std::string candidate = "<D:multistatus>" + pending_.substr(0, closeEnd + 1) + "</D:multistatus>";
    const ParseResult parsed = parsePropfind207(candidate, expectedOrigin_);
    if (!parsed.ok() || parsed.entries.size() != 1u) {
      error_ = parsed.error == ParseError::none ? ParseError::malformed_xml : parsed.error;
      return false;
    }
    if (!sink || !sink(parsed.entries.front(), context)) {
      sinkAborted_ = true;
      return false;
    }
    ++entries_;
    pending_.erase(0, closeEnd + 1);
    inResponse_ = false;
  }
}

bool PropfindStreamParser::feed(const std::uint8_t* data, const std::size_t size, const PropfindEntrySink sink,
                                void* const context) {
  if (error_ != ParseError::none || sinkAborted_ || (!data && size != 0)) return false;
  if (size > WebDavPullLimits::maxEntry + WebDavPullLimits::maxXmlText ||
      pending_.size() > WebDavPullLimits::maxEntry + WebDavPullLimits::maxXmlText - size) {
    error_ = ParseError::oversized_xml;
    return false;
  }
  pending_.append(reinterpret_cast<const char*>(data), size);
  return consume(sink, context);
}

bool PropfindStreamParser::finish() {
  if (error_ != ParseError::none || sinkAborted_ || inResponse_ || !sawRoot_) return false;
  std::size_t cursor = 0;
  while (cursor < pending_.size() && std::isspace(static_cast<unsigned char>(pending_[cursor]))) ++cursor;
  if (cursor != pending_.size() && !sawRootClose_) {
    error_ = ParseError::malformed_xml;
    return false;
  }
  if (!sawRootClose_) {
    error_ = ParseError::malformed_xml;
    return false;
  }
  pending_.clear();
  return true;
}

bool redirectAllowed(std::string_view currentUrl, std::string_view nextUrl) {
  UrlOrigin current;
  UrlOrigin next;
  if (!parseOrigin(currentUrl, current) || !parseOrigin(nextUrl, next)) return false;
  if (current.scheme == "https" && next.scheme != "https") return false;
  if (current.host != next.host) return false;
  return current.port == next.port || (current.port == 80u && next.port == 443u);
}

bool validLocalPath(std::string_view path) {
  if (path.empty() || path.front() != '/' || path.find('\\') != std::string_view::npos) return false;
  for (const unsigned char c : path) {
    if (c < 32 || c == 127) return false;
  }
  for (std::size_t start = 1; start <= path.size();) {
    const std::size_t end = std::min(path.find('/', start), path.size());
    const std::string_view part = path.substr(start, end - start);
    if (part.empty() || part == "." || part == "..") return false;
    start = end + 1;
  }
  return true;
}

bool isValidEpub(std::string_view bytes) {
  if (bytes.size() < 30 || bytes.substr(0, 4) != "PK\x03\x04") return false;
  const auto read16 = [&bytes](std::size_t offset) -> std::uint16_t {
    return static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[offset])) |
           static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[offset + 1])) << 8;
  };
  const std::size_t nameLength = read16(26);
  const std::size_t extraLength = read16(28);
  if (read16(8) != 0 || 30u + nameLength + extraLength > bytes.size() || nameLength != 8 ||
      bytes.substr(30, 8) != "mimetype")
    return false;
  const std::size_t contentStart = 30u + nameLength + extraLength;
  const std::string_view media = "application/epub+zip";
  return contentStart + media.size() <= bytes.size() && bytes.substr(contentStart, media.size()) == media;
}

std::size_t PullQueue::itemBytes(const PropfindEntry& entry) {
  return 64u + entry.href.size() + entry.path.size() + entry.contentType.size();
}

PullStatus downloadStatus(const PropfindEntry& entry, const bool destinationExists, const bool downloadSucceeded,
                          const bool epubValid) {
  if (destinationExists) return PullStatus::skipped_existing;
  if (entry.collection || entry.path.size() < 5 || lower(entry.path.substr(entry.path.size() - 5)) != ".epub")
    return PullStatus::invalid;
  // A transfer that did not arrive whole is failed, whatever its first bytes looked like.
  if (!downloadSucceeded) return PullStatus::failed;
  return epubValid ? PullStatus::downloaded : PullStatus::invalid;
}

// A broken file is that file's problem; the network or the card failing ends the pull.
bool pullContinuesAfter(const PullStatus status) { return status != PullStatus::failed; }

QueueResult PullQueue::submit(const PropfindEntry& entry, bool destinationExists, bool downloadSucceeded,
                              bool epubValid) {
  if (destinationExists) return {PullStatus::skipped_existing, false};
  if (std::find(paths_.begin(), paths_.end(), entry.path) != paths_.end()) return {PullStatus::collision, false};
  const PullStatus status = downloadStatus(entry, false, downloadSucceeded, epubValid);
  if (status != PullStatus::downloaded) return {status, false};
  const std::size_t cost = itemBytes(entry);
  if (cost > WebDavPullLimits::maxEntry) return {PullStatus::invalid, false};
  if (cost > WebDavPullLimits::maxQueueBytes || bytes_ > WebDavPullLimits::maxQueueBytes - cost)
    return {PullStatus::failed, false};
  bytes_ += cost;
  paths_.push_back(entry.path);
  items_.push_back(entry);
  return {PullStatus::downloaded, true};
}

}  // namespace webdav
