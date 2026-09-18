#pragma once

#include <I18n.h>
#include <WebServer.h>

#include <cstdint>
#include <string>
#include <string_view>

#include "WebPathPolicy.h"

#ifdef SIMULATOR
#include <cstdlib>
#endif

// Guards the transfer server against requests that did not come from the page it
// serves: a request is accepted only when its Host is the reader's own address
// (IP or <device name>.local, on the served port) and a present Origin matches
// it. That rejects DNS-rebinding and cross-site requests. There is no credential
// gate: opening the transfer page never asks for a password.
class WebTransferAuth {
 public:
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
    if (hostAllowed(host.c_str()) && (origin.isEmpty() || origin == "http://" + host)) return true;
    if (reply) {
      server.send(403, "text/plain", I18n::getInstance().get(StrId::STR_WEB_FORBIDDEN_ORIGIN, language));
    }
    return false;
  }

 private:
  uint16_t port_ = 80;
  std::string ip_;
  std::string hostname_;
};
