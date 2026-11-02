#pragma once

#include <cstddef>
#include <cstdint>

struct mbedtls_gcm_context {};
constexpr int MBEDTLS_CIPHER_ID_AES = 2;
constexpr int MBEDTLS_GCM_ENCRYPT = 1;

inline void mbedtls_gcm_init(mbedtls_gcm_context *) {}
inline void mbedtls_gcm_free(mbedtls_gcm_context *) {}
inline int mbedtls_gcm_setkey(
    mbedtls_gcm_context *, int, const unsigned char *, unsigned int) {
  return -1;
}
inline int mbedtls_gcm_crypt_and_tag(
    mbedtls_gcm_context *, int, size_t, const unsigned char *, size_t,
    const unsigned char *, size_t, const unsigned char *, unsigned char *,
    size_t, unsigned char *) {
  return -1;
}
inline int mbedtls_gcm_auth_decrypt(
    mbedtls_gcm_context *, size_t, const unsigned char *, size_t,
    const unsigned char *, size_t, const unsigned char *, size_t,
    const unsigned char *, unsigned char *) {
  return -1;
}
