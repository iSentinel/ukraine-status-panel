#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#ifdef USE_ESP32
#include <esp_random.h>
#include <mbedtls/sha256.h>
#endif

namespace usp_spotify_pkce {

constexpr size_t VERIFIER_BYTES = 32;
constexpr size_t VERIFIER_CHARS = 43;
constexpr size_t STATE_BYTES = 16;
constexpr size_t STATE_CHARS = 22;
constexpr size_t SHA256_BYTES = 32;
constexpr uint32_t SESSION_TTL_MS = 5U * 60U * 1000U;
constexpr size_t MAX_CLIENT_ID = 128;
constexpr size_t MAX_REDIRECT_URI = 256;
constexpr char AUTHORIZE_ENDPOINT[] = "https://accounts.spotify.com/authorize";

inline bool ascii_alnum(char ch) {
  return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
         (ch >= '0' && ch <= '9');
}

inline size_t base64url_no_pad(const uint8_t *input, size_t length,
                               char *output, size_t capacity) {
  static constexpr char TABLE[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  if (input == nullptr || output == nullptr) return 0;
  const size_t required = (length * 4U + 2U) / 3U;
  if (capacity <= required) return 0;
  size_t in = 0;
  size_t out = 0;
  while (in + 3U <= length) {
    const uint32_t value = (uint32_t(input[in]) << 16U) |
                           (uint32_t(input[in + 1U]) << 8U) |
                           uint32_t(input[in + 2U]);
    output[out++] = TABLE[(value >> 18U) & 0x3FU];
    output[out++] = TABLE[(value >> 12U) & 0x3FU];
    output[out++] = TABLE[(value >> 6U) & 0x3FU];
    output[out++] = TABLE[value & 0x3FU];
    in += 3U;
  }
  if (length - in == 1U) {
    const uint32_t value = uint32_t(input[in]) << 16U;
    output[out++] = TABLE[(value >> 18U) & 0x3FU];
    output[out++] = TABLE[(value >> 12U) & 0x3FU];
  } else if (length - in == 2U) {
    const uint32_t value = (uint32_t(input[in]) << 16U) |
                           (uint32_t(input[in + 1U]) << 8U);
    output[out++] = TABLE[(value >> 18U) & 0x3FU];
    output[out++] = TABLE[(value >> 12U) & 0x3FU];
    output[out++] = TABLE[(value >> 6U) & 0x3FU];
  }
  output[out] = '\0';
  return out;
}

inline void zeroize(char *data, size_t length) {
  volatile char *bytes = data;
  for (size_t index = 0; index < length; ++index) bytes[index] = 0;
}

inline bool constant_time_equal(std::string_view left,
                                std::string_view right) {
  if (left.size() != right.size()) return false;
  uint8_t difference = 0;
  for (size_t index = 0; index < left.size(); ++index)
    difference |= uint8_t(left[index]) ^ uint8_t(right[index]);
  return difference == 0;
}

inline std::string url_encode(std::string_view value) {
  static constexpr char HEX[] = "0123456789ABCDEF";
  std::string encoded;
  encoded.reserve(value.size() * 3U);
  for (const unsigned char ch : value) {
    if (ascii_alnum(char(ch)) || ch == '-' || ch == '_' || ch == '.' ||
        ch == '~') {
      encoded.push_back(char(ch));
      continue;
    }
    encoded.push_back('%');
    encoded.push_back(HEX[(ch >> 4U) & 0x0FU]);
    encoded.push_back(HEX[ch & 0x0FU]);
  }
  return encoded;
}

inline bool valid_client_id(std::string_view value) {
  if (value.empty() || value.size() > MAX_CLIENT_ID) return false;
  for (const char ch : value)
    if (!ascii_alnum(ch)) return false;
  return true;
}

inline bool valid_https_redirect(std::string_view value) {
  if (value.size() <= 8U || value.size() > MAX_REDIRECT_URI ||
      value.substr(0, 8) != "https://") return false;
  for (const unsigned char ch : value)
    if (ch <= 0x20U || ch == 0x7FU || ch == '#') return false;
  return true;
}

struct Session {
  bool active{false};
  uint32_t started_ms{0};
  std::array<char, VERIFIER_CHARS + 1U> verifier{};
  std::array<char, VERIFIER_CHARS + 1U> challenge{};
  std::array<char, STATE_CHARS + 1U> state{};

  void clear() {
    zeroize(verifier.data(), verifier.size());
    zeroize(challenge.data(), challenge.size());
    zeroize(state.data(), state.size());
    active = false;
    started_ms = 0;
  }
};

struct Grant {
  bool valid{false};
  std::array<char, VERIFIER_CHARS + 1U> verifier{};

  void clear() {
    zeroize(verifier.data(), verifier.size());
    valid = false;
  }
};

inline bool begin_from_material(
    Session *session, uint32_t now,
    const std::array<uint8_t, VERIFIER_BYTES> &verifier_entropy,
    const std::array<uint8_t, STATE_BYTES> &state_entropy,
    const std::array<uint8_t, SHA256_BYTES> &verifier_digest) {
  if (session == nullptr) return false;
  session->clear();
  const bool verifier_ok = base64url_no_pad(
      verifier_entropy.data(), verifier_entropy.size(), session->verifier.data(),
      session->verifier.size()) == VERIFIER_CHARS;
  const bool challenge_ok = base64url_no_pad(
      verifier_digest.data(), verifier_digest.size(), session->challenge.data(),
      session->challenge.size()) == VERIFIER_CHARS;
  const bool state_ok = base64url_no_pad(
      state_entropy.data(), state_entropy.size(), session->state.data(),
      session->state.size()) == STATE_CHARS;
  if (!verifier_ok || !challenge_ok || !state_ok) {
    session->clear();
    return false;
  }
  session->started_ms = now;
  session->active = true;
  return true;
}

#ifdef USE_ESP32
inline bool sha256(std::string_view value,
                   std::array<uint8_t, SHA256_BYTES> *digest) {
  if (digest == nullptr) return false;
  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  bool ok = mbedtls_sha256_starts(&context, 0) == 0;
  if (ok)
    ok = mbedtls_sha256_update(
             &context, reinterpret_cast<const unsigned char *>(value.data()),
             value.size()) == 0;
  if (ok) ok = mbedtls_sha256_finish(&context, digest->data()) == 0;
  mbedtls_sha256_free(&context);
  return ok;
}

inline bool begin(Session *session, uint32_t now) {
  if (session == nullptr) return false;
  std::array<uint8_t, VERIFIER_BYTES> verifier_entropy{};
  std::array<uint8_t, STATE_BYTES> state_entropy{};
  std::array<uint8_t, SHA256_BYTES> digest{};
  esp_fill_random(verifier_entropy.data(), verifier_entropy.size());
  esp_fill_random(state_entropy.data(), state_entropy.size());
  char verifier[VERIFIER_CHARS + 1U]{};
  const bool encoded = base64url_no_pad(
      verifier_entropy.data(), verifier_entropy.size(), verifier,
      sizeof(verifier)) == VERIFIER_CHARS;
  const bool hashed = encoded && sha256(verifier, &digest);
  const bool started = hashed && begin_from_material(
      session, now, verifier_entropy, state_entropy, digest);
  zeroize(verifier, sizeof(verifier));
  zeroize(reinterpret_cast<char *>(verifier_entropy.data()), verifier_entropy.size());
  zeroize(reinterpret_cast<char *>(state_entropy.data()), state_entropy.size());
  zeroize(reinterpret_cast<char *>(digest.data()), digest.size());
  return started;
}
#endif

enum class CallbackResult : uint8_t {
  ACCEPT_CODE,
  ACCEPT_ERROR,
  REJECT_INACTIVE,
  REJECT_EXPIRED,
  REJECT_STATE,
  REJECT_MALFORMED,
};

inline CallbackResult consume_callback(Session *session, uint32_t now,
                                       std::string_view supplied_state,
                                       bool has_code, bool has_error,
                                       Grant *grant = nullptr) {
  if (grant != nullptr) grant->clear();
  if (session == nullptr || !session->active)
    return CallbackResult::REJECT_INACTIVE;
  if (now - session->started_ms >= SESSION_TTL_MS) {
    session->clear();
    return CallbackResult::REJECT_EXPIRED;
  }
  if (!constant_time_equal(supplied_state, session->state.data()))
    return CallbackResult::REJECT_STATE;
  CallbackResult result = CallbackResult::REJECT_MALFORMED;
  if (has_code != has_error)
    result = has_code ? CallbackResult::ACCEPT_CODE
                      : CallbackResult::ACCEPT_ERROR;
  if (result == CallbackResult::ACCEPT_CODE && grant != nullptr) {
    grant->verifier = session->verifier;
    grant->valid = true;
  }
  session->clear();
  return result;
}

inline std::string authorization_url(std::string_view client_id,
                                     std::string_view redirect_uri,
                                     std::string_view scope,
                                     const Session &session) {
  if (!session.active || !valid_client_id(client_id) ||
      !valid_https_redirect(redirect_uri)) return {};
  std::string url = AUTHORIZE_ENDPOINT;
  url += "?response_type=code&client_id=" + url_encode(client_id);
  url += "&redirect_uri=" + url_encode(redirect_uri);
  url += "&code_challenge_method=S256&code_challenge=";
  url += session.challenge.data();
  url += "&state=";
  url += session.state.data();
  if (!scope.empty()) url += "&scope=" + url_encode(scope);
  return url;
}

}  // namespace usp_spotify_pkce
