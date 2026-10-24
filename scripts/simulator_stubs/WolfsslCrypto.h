#pragma once

// Simulator stand-in for the wolfSSL backend (the real header is
// freeink-sdk/libs/book/ContentProtection/include/WolfsslCrypto.h). The simulator links no wolfSSL and has no
// TLS to reach an LCP license server, so LCP and protected books are off here: every operation refuses and
// the digests are zero. Force-included in C++ units by scripts/patch_simulator_gaps.py (the SDK header is empty without FREEINK_CONTENT_WOLFSSL).

#include "../../freeink-sdk/libs/book/ContentProtection/include/Crypto.h"

#include <cstring>

namespace freeink {
namespace content {

class WolfsslCrypto : public Crypto {
 public:
  std::string lastError = "no crypto backend in the simulator";

  int32_t rsaPrivateRaw(const uint8_t*, size_t, const uint8_t*, size_t, uint8_t*, size_t) override { return -1; }
  bool aes128CbcDecrypt(const uint8_t*, const uint8_t*, const uint8_t*, size_t, uint8_t*) override { return false; }
  void sha1(const uint8_t*, size_t, uint8_t out[20]) override { memset(out, 0, 20); }
  void sha256(const uint8_t*, size_t, uint8_t out[32]) override { memset(out, 0, 32); }
  bool rsaGenerate(RsaKeyPairDer*) override { return false; }
  bool rsaPublicEncrypt(const uint8_t*, size_t, const uint8_t*, size_t, uint8_t*, size_t, size_t*) override {
    return false;
  }
  bool rsaPrivateSignRaw(const uint8_t*, size_t, const uint8_t[20], uint8_t[128]) override { return false; }
  bool aes128CbcEncrypt(const uint8_t*, const uint8_t*, const uint8_t*, size_t, uint8_t*) override { return false; }
  bool pkcs12Extract(const uint8_t*, size_t, const std::string&, std::vector<uint8_t>*,
                     std::vector<uint8_t>*) override {
    return false;
  }
  void randomBytes(uint8_t* out, size_t len) override { memset(out, 0, len); }
};

}  // namespace content
}  // namespace freeink
