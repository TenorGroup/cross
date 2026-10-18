#include "WebDavConfigStore.h"

#include <ObfuscationUtils.h>

#include <cctype>
#include <utility>

namespace {
bool validAuthority(std::string_view authority) {
  if (authority.empty() || authority.find('@') != std::string_view::npos ||
      authority.find('\\') != std::string_view::npos)
    return false;

  const size_t colon = authority.rfind(':');
  const std::string_view host = colon == std::string_view::npos ? authority : authority.substr(0, colon);
  if (host.empty()) return false;
  if (colon != std::string_view::npos) {
    const std::string_view port = authority.substr(colon + 1);
    if (port.empty() || port.size() > 5) return false;
    unsigned value = 0;
    for (const char c : port) {
      if (c < '0' || c > '9') return false;
      value = value * 10u + static_cast<unsigned>(c - '0');
      if (value > 65535u) return false;
    }
  }
  for (const unsigned char c : authority) {
    if (std::isspace(c) || c < 32 || c == 127) return false;
  }
  return true;
}
}  // namespace

void WebDavConfigStore::toJson(JsonDocument& doc) const {
  doc["url"] = config.url;
  doc["username"] = config.username;
  doc["password_obf"] = obfuscation::obfuscateToBase64(config.password);
}

bool WebDavConfigStore::fromJson(JsonVariantConst doc) {
  WebDavConfig loaded;
  loaded.url = doc["url"] | "";
  loaded.username = doc["username"] | "";

  bool needsResave = false;
  bool passwordValid = false;
  loaded.password = extractPassword(doc, needsResave, MAX_PASSWORD_LENGTH, passwordValid);
  if (!passwordValid || loaded.username.size() > MAX_USERNAME_LENGTH || !isValid(loaded)) {
    loaded = {};
    needsResave = true;
  }
  config = std::move(loaded);
  if (needsResave) requestResave();
  return true;
}

bool WebDavConfigStore::isValidUrl(const std::string_view url) {
  if (url.empty() || url.size() > MAX_URL_LENGTH) return false;
  const size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string_view::npos || (schemeEnd != 4 && schemeEnd != 5)) return false;
  const std::string_view scheme = url.substr(0, schemeEnd);
  if (!((scheme == "http") || (scheme == "https"))) return false;
  const size_t authorityStart = schemeEnd + 3;
  const size_t authorityEnd = url.find_first_of("/?#", authorityStart);
  const std::string_view authority = url.substr(
      authorityStart, authorityEnd == std::string_view::npos ? url.size() - authorityStart : authorityEnd - authorityStart);
  if (!validAuthority(authority)) return false;
  for (const unsigned char c : url) {
    if (c < 32 || c == 127 || std::isspace(c)) return false;
  }
  // Credentials belong in the config fields and are never accepted in a URL.
  return url.find('?') == std::string_view::npos && url.find('#') == std::string_view::npos;
}

bool WebDavConfigStore::isValid(const WebDavConfig& value) {
  return isValidUrl(value.url) && value.username.size() <= MAX_USERNAME_LENGTH &&
         value.password.size() <= MAX_PASSWORD_LENGTH;
}

bool WebDavConfigStore::setConfig(const WebDavConfig& value) {
  if (value.url.empty()) {
    if (!value.username.empty() || !value.password.empty()) return false;
    return clear();
  }
  if (!isValid(value)) return false;
  const WebDavConfig old = config;
  config = value;
  if (saveToFile()) return true;
  config = old;
  return false;
}

bool WebDavConfigStore::clear() {
  const WebDavConfig old = config;
  config = {};
  if (saveToFile()) return true;
  config = old;
  return false;
}
