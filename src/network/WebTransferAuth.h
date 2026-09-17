#pragma once

#include <I18n.h>
#include <WebServer.h>
#include <base64.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "WebPathPolicy.h"

#ifdef SIMULATOR
#include <cstdlib>
#else
#include <esp_random.h>
#endif

class WebTransferAuth {
 public:
  WebTransferAuth() {
    randomHex(password_, 8);
    randomHex(token_, 16);
#ifdef SIMULATOR
    const char* fixture = std::getenv("CROSSPOINT_SIM_TRANSFER_PASSWORD");
    if (fixture && std::strlen(fixture) == 16) std::memcpy(password_, fixture, 16);
#endif
    expectedAuth_ = "Basic " + std::string(base64::encode((std::string("tenor:") + password_).c_str()).c_str());
  }

  const char* password() const { return password_; }
  const char* token() const { return token_; }
  void configure(const String& ip, const char* hostname) {
    ip_ = ip.c_str();
    hostname_ = std::string(hostname) + ".local";
#ifdef SIMULATOR
    port_ = 8080;
    const char* configured = std::getenv("CROSSPOINT_SIM_HTTP_PORT");
    if (configured) {
      const auto value = std::strtoul(configured, nullptr, 10);
      if (value >= 1024 && value <= 65534) port_ = static_cast<uint16_t>(value);
    }
#endif
  }

  bool hostAllowed(std::string_view authority) const {
    const auto colon = authority.find(':');
    const auto host = authority.substr(0, colon);
    if (colon != std::string_view::npos) {
      const auto port = authority.substr(colon + 1);
      if (port.empty() || port.size() > 5) return false;
      unsigned value = 0;
      for (char c : port) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
      }
      if (value != port_) return false;
    }
    return web_path::equalsFolded(host, ip_) || web_path::equalsFolded(host, hostname_);
  }

  bool originAllowed(std::string_view origin) const {
    constexpr std::string_view prefix = "http://";
    return origin.substr(0, prefix.size()) == prefix && hostAllowed(origin.substr(prefix.size()));
  }

  // The language of the rejection body is the request's locale; callers that
  // serve a non-localised surface (WebDAV) keep the English default.
  bool authorize(WebServer& server, bool reply = true, Language language = Language::EN) const {
    const String host = server.header("Host");
    const String origin = server.header("Origin");
    if (!hostAllowed(host.c_str()) || (!origin.isEmpty() && origin != "http://" + host)) {
      if (reply) {
        server.send(403, "text/plain", I18n::getInstance().get(StrId::STR_WEB_FORBIDDEN_ORIGIN, language));
      }
      return false;
    }
    const String value = server.header("Authorization");
    if (equalSecret(std::string_view(value.c_str(), value.length()), expectedAuth_)) return true;
    if (reply) {
      server.sendHeader("WWW-Authenticate", "Basic realm=\"tenor/cross\"");
      server.sendHeader("Cache-Control", "no-store");
      server.send(401, "text/plain", I18n::getInstance().get(StrId::STR_WEB_AUTH_REQUIRED, language));
    }
    return false;
  }

  bool tokenMatches(std::string_view supplied) const { return equalSecret(supplied, token_); }

 private:
  char password_[17] = {};
  char token_[33] = {};
  uint16_t port_ = 80;
  std::string expectedAuth_;
  std::string ip_;
  std::string hostname_;

  static bool equalSecret(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    unsigned mismatch = 0;
    for (size_t i = 0; i < a.size(); ++i) mismatch |= static_cast<unsigned char>(a[i] ^ b[i]);
    return mismatch == 0;
  }

  static void randomHex(char* out, size_t byteCount) {
    uint8_t bytes[16];
#ifdef SIMULATOR
    arc4random_buf(bytes, byteCount);
#else
    esp_fill_random(bytes, byteCount);
#endif
    constexpr char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < byteCount; ++i) {
      out[i * 2] = hex[bytes[i] >> 4];
      out[i * 2 + 1] = hex[bytes[i] & 15];
    }
    out[byteCount * 2] = 0;
  }
};
