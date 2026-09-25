#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
// Every image digests to zeros here; the offered manifest digest is zeros too.
struct mbedtls_sha256_context {};
inline void mbedtls_sha256_init(mbedtls_sha256_context*) {}
inline int mbedtls_sha256_starts(mbedtls_sha256_context*, int) { return 0; }
inline int mbedtls_sha256_update(mbedtls_sha256_context*, const unsigned char*, size_t) { return 0; }
inline int mbedtls_sha256_finish(mbedtls_sha256_context*, unsigned char out[32]) {
  std::memset(out, 0, 32);
  return 0;
}
inline void mbedtls_sha256_free(mbedtls_sha256_context*) {}
