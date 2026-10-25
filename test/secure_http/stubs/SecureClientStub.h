#pragma once
#include "SecureHttpClient.h"
// The TLS client over the in-memory wire (WiFiClient.h). Every https connect leaves the verification it was made
// with (skipped, or against which roots) in wire::tls.
namespace freeink {
SecureClient::~SecureClient() { stop(); }
void SecureClient::setCACert(const char* ca) { _rootCA = ca; }
void SecureClient::setInsecure() { _insecure = true; }
void SecureClient::setReuseTlsContext(bool reuse) { _reuseTlsContext = reuse; }
int SecureClient::connect(IPAddress ip, uint16_t p) { return _transport.connect(ip, p); }
int SecureClient::connect(const char* h, uint16_t p) {
  wire::tls.push_back({_insecure, _rootCA});
  return _transport.connect(h, p);
}
size_t SecureClient::write(uint8_t b) { return _transport.write(b); }
size_t SecureClient::write(const uint8_t* p, size_t n) { return _transport.write(p, n); }
int SecureClient::available() { return _transport.available(); }
int SecureClient::read() { return _transport.read(); }
int SecureClient::read(uint8_t* p, size_t n) { return _transport.read(p, n); }
int SecureClient::peek() { return _transport.peek(); }
void SecureClient::flush() { _transport.flush(); }
void SecureClient::stop() { _transport.stop(); }
uint8_t SecureClient::connected() { return _transport.connected(); }
}  // namespace freeink
