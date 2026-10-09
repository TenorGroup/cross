#pragma once
#include <openssl/sha.h>
using mbedtls_sha256_context = SHA256_CTX;
inline void mbedtls_sha256_init(mbedtls_sha256_context*) {}
inline void mbedtls_sha256_free(mbedtls_sha256_context*) {}
inline int mbedtls_sha256_starts(mbedtls_sha256_context* state, int) { return SHA256_Init(state) == 1 ? 0 : -1; }
inline int mbedtls_sha256_update(mbedtls_sha256_context* state, const unsigned char* bytes, size_t count) {
  return SHA256_Update(state, bytes, count) == 1 ? 0 : -1;
}
inline int mbedtls_sha256_finish(mbedtls_sha256_context* state, unsigned char* result) {
  return SHA256_Final(result, state) == 1 ? 0 : -1;
}
