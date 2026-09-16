#pragma once
#include <cstddef>
// Ownership and configuration doubles only. These tests do not validate TLS cryptography.
struct WOLFSSL_METHOD {};
struct WOLFSSL_CTX {
  WOLFSSL_METHOD* method;
};
struct WOLFSSL {};
namespace tls_fixture {
inline int outstandingMethods = 0, verifyMode = 0;
inline bool domainChecked = false;
}  // namespace tls_fixture
constexpr int WOLFSSL_SUCCESS = 1, WOLFSSL_CBIO_ERR_CONN_CLOSE = -5, WOLFSSL_CBIO_ERR_WANT_WRITE = -2,
              WOLFSSL_CBIO_ERR_WANT_READ = -3;
constexpr int WOLFSSL_ERROR_WANT_READ = 2, WOLFSSL_ERROR_WANT_WRITE = 3, WOLFSSL_ERROR_ZERO_RETURN = 6;
constexpr int WOLFSSL_VERIFY_NONE = 0, WOLFSSL_VERIFY_PEER = 1, WOLFSSL_FILETYPE_PEM = 1, WOLFSSL_SNI_HOST_NAME = 0;
inline WOLFSSL_METHOD* wolfSSLv23_client_method() {
  ++tls_fixture::outstandingMethods;
  return new WOLFSSL_METHOD;
}
inline WOLFSSL_METHOD* wolfTLSv1_2_client_method() { return wolfSSLv23_client_method(); }
inline WOLFSSL_CTX* wolfSSL_CTX_new(WOLFSSL_METHOD* m) { return new WOLFSSL_CTX{m}; }
inline void wolfSSL_CTX_free(WOLFSSL_CTX* c) {
  delete c->method;
  delete c;
  --tls_fixture::outstandingMethods;
}
inline void wolfSSL_CTX_set_verify(WOLFSSL_CTX*, int mode, void*) { tls_fixture::verifyMode = mode; }
inline int wolfSSL_CTX_load_verify_buffer(WOLFSSL_CTX*, const unsigned char*, size_t, int) { return WOLFSSL_SUCCESS; }
inline void wolfSSL_SetIORecv(WOLFSSL_CTX*, int (*)(WOLFSSL*, char*, int, void*)) {}
inline void wolfSSL_SetIOSend(WOLFSSL_CTX*, int (*)(WOLFSSL*, char*, int, void*)) {}
inline WOLFSSL* wolfSSL_new(WOLFSSL_CTX*) { return new WOLFSSL; }
inline void wolfSSL_free(WOLFSSL* s) { delete s; }
inline int wolfSSL_check_domain_name(WOLFSSL*, const char*) {
  tls_fixture::domainChecked = true;
  return WOLFSSL_SUCCESS;
}
inline void wolfSSL_SetIOReadCtx(WOLFSSL*, void*) {}
inline void wolfSSL_SetIOWriteCtx(WOLFSSL*, void*) {}
inline int wolfSSL_UseSNI(WOLFSSL*, int, const char*, size_t) { return WOLFSSL_SUCCESS; }
inline int wolfSSL_connect(WOLFSSL*) { return WOLFSSL_SUCCESS; }
inline int wolfSSL_get_error(WOLFSSL*, int) { return 0; }
inline const char* wolfSSL_get_version(WOLFSSL*) { return "fixture"; }
inline const char* wolfSSL_get_cipher(WOLFSSL*) { return "fixture"; }
inline int wolfSSL_write(WOLFSSL*, const void*, int n) { return n; }
inline int wolfSSL_read(WOLFSSL*, void*, int) { return 0; }
inline int wolfSSL_pending(WOLFSSL*) { return 0; }
