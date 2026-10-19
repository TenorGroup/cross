#pragma once
#include <openssl/evp.h>
#include <cstdio>
#include <cstring>
#include <string>

class MD5Builder {
 public:
  ~MD5Builder() { if (ctx) EVP_MD_CTX_free(ctx); }
  void begin() { if (ctx) EVP_MD_CTX_free(ctx); ctx = EVP_MD_CTX_new(); EVP_DigestInit_ex(ctx, EVP_md5(), nullptr); }
  void add(const uint8_t* data, size_t n) { EVP_DigestUpdate(ctx, data, n); }
  void add(const char* text) { add(reinterpret_cast<const uint8_t*>(text), std::strlen(text)); }
  void calculate() {
    unsigned char digest[16];
    EVP_DigestFinal_ex(ctx, digest, nullptr);
    hex.clear();
    char two[3];
    for (unsigned char b : digest) { std::snprintf(two, sizeof(two), "%02x", b); hex += two; }
  }
  std::string toString() const { return hex; }

 private:
  EVP_MD_CTX* ctx = nullptr;
  std::string hex;
};
