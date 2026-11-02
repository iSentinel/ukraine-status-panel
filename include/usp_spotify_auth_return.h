#pragma once

#include "usp_spotify_pkce.h"
#include <algorithm>
#include <array>
#include <string_view>

namespace usp_spotify_auth_return {

constexpr size_t MAX_CODE = 1024;
constexpr size_t MAX_ERROR = 128;
constexpr uint32_t GRANT_TTL_MS = 60U * 1000U;
constexpr char SCOPES[] =
    "user-read-playback-state user-read-currently-playing user-modify-playback-state";

enum class Status : uint8_t { IDLE, AWAITING_USER, CODE_PENDING };

/** Reject controls and oversized opaque fields without truncating a code. */
inline bool safe_field(std::string_view value, size_t limit) {
  if (value.size() > limit) return false;
  for (unsigned char ch : value)
    if (ch < 0x20U || ch > 0x7EU) return false;
  return true;
}

/** Validate one DNS-style authority, with an optional explicit TCP port. */
inline bool valid_authority(std::string_view authority) {
  const auto colon = authority.find(':');
  const auto host = authority.substr(0, colon);
  if (host.empty() || host.front() == '.' || host.back() == '.') return false;
  size_t label_size = 0;
  for (size_t i = 0; i < host.size(); ++i) {
    const char ch = host[i];
    if (ch == '.') {
      if (!label_size || host[i - 1] == '-') return false;
      label_size = 0;
    } else {
      if (!usp_spotify_pkce::ascii_alnum(ch) && ch != '-') return false;
      if (!label_size && ch == '-') return false;
      if (++label_size > 63) return false;
    }
  }
  if (!label_size || host.back() == '-') return false;
  if (colon == std::string_view::npos) return true;
  const auto port = authority.substr(colon + 1);
  if (port.empty() || port.size() > 5) return false;
  uint32_t number = 0;
  for (char ch : port) {
    if (ch < '0' || ch > '9') return false;
    number = number * 10U + uint32_t(ch - '0');
  }
  return number > 0 && number <= 65535;
}

/** A hosted callback has a fixed HTTPS path, with no preset query or fragment. */
inline bool valid_redirect(std::string_view uri) {
  if (!usp_spotify_pkce::valid_https_redirect(uri) ||
      uri.find('?') != std::string_view::npos || !safe_field(uri, 256))
    return false;
  const auto end = uri.find('/', 8);
  return valid_authority(uri.substr(8, end == uri.npos ? uri.npos : end - 8));
}

template<size_t N>
inline bool copy_field(std::array<char, N> &target, std::string_view value) {
  target.fill(0);
  if (value.size() >= N) return false;
  std::copy(value.begin(), value.end(), target.begin());
  return true;
}

/** Ephemeral token-worker handoff; never publish these fields to Web or logs. */
struct TokenRequest {
  std::array<char, MAX_CODE + 1> code{};
  std::array<char, usp_spotify_pkce::VERIFIER_CHARS + 1> verifier{};
  std::array<char, usp_spotify_pkce::MAX_CLIENT_ID + 1> client_id{};
  std::array<char, usp_spotify_pkce::MAX_REDIRECT_URI + 1> redirect_uri{};
  TokenRequest() = default;
  TokenRequest(const TokenRequest &) = delete;
  TokenRequest &operator=(const TokenRequest &) = delete;
  ~TokenRequest() { clear(); }

  void clear() {
    usp_spotify_pkce::zeroize(code.data(), code.size());
    usp_spotify_pkce::zeroize(verifier.data(), verifier.size());
    usp_spotify_pkce::zeroize(client_id.data(), client_id.size());
    usp_spotify_pkce::zeroize(redirect_uri.data(), redirect_uri.size());
  }
};

/** Caller owns synchronization; no provider, storage, HTTP or device operations. */
class Exchange {
 public:
  Exchange() = default;
  Exchange(const Exchange &) = delete;
  Exchange &operator=(const Exchange &) = delete;
  ~Exchange() { cancel(); }

  /** Consume a freshly generated session, including on rejection. */
  bool adopt(usp_spotify_pkce::Session *prepared, uint32_t now,
             std::string_view client, std::string_view redirect) {
    expire(now);
    if (prepared == nullptr) return false;
    const bool valid = !session_.active && !pending_ && prepared->active &&
        now - prepared->started_ms < usp_spotify_pkce::SESSION_TTL_MS &&
        usp_spotify_pkce::valid_client_id(client) && valid_redirect(redirect);
    if (valid) {
      session_ = *prepared;
      copy_field(client_id_, client);
      copy_field(redirect_uri_, redirect);
    }
    prepared->clear();
    return valid;
  }

  std::string authorization_url(uint32_t now) {
    expire(now);
    return usp_spotify_pkce::authorization_url(
        client_id_.data(), redirect_uri_.data(), SCOPES, session_);
  }

  /** Preserve an active session for wrong-state callbacks; consume valid ones. */
  usp_spotify_pkce::CallbackResult accept(
      uint32_t now, std::string_view state, std::string_view code,
      std::string_view error) {
    expire(now);
    if (pending_) return usp_spotify_pkce::CallbackResult::REJECT_INACTIVE;
    usp_spotify_pkce::Grant grant;
    const bool well_formed = safe_field(code, MAX_CODE) &&
                              safe_field(error, MAX_ERROR);
    const auto result = usp_spotify_pkce::consume_callback(
        &session_, now, state, well_formed && !code.empty(),
        well_formed && !error.empty(), &grant);
    if (result == usp_spotify_pkce::CallbackResult::ACCEPT_CODE) {
      copy_field(code_, code);
      verifier_ = grant.verifier;
      grant.clear();
      pending_ = true;
      received_ms_ = now;
    } else if (!session_.active) {
      clear_config_();
    }
    return result;
  }

  /** Issue a bounded one-time handoff, then erase the inbox. */
  bool take(uint32_t now, TokenRequest *output) {
    if (output == nullptr) return false;
    output->clear();
    expire(now);
    if (!pending_) return false;
    output->code = code_;
    output->verifier = verifier_;
    output->client_id = client_id_;
    output->redirect_uri = redirect_uri_;
    cancel();
    return true;
  }

  Status status(uint32_t now) {
    expire(now);
    return pending_ ? Status::CODE_PENDING :
        (session_.active ? Status::AWAITING_USER : Status::IDLE);
  }

  /** Use subtraction so both deadlines survive the millisecond counter wrap. */
  void expire(uint32_t now) {
    if ((session_.active &&
         now - session_.started_ms >= usp_spotify_pkce::SESSION_TTL_MS) ||
        (pending_ && now - received_ms_ >= GRANT_TTL_MS))
      cancel();
  }

  void cancel() {
    session_.clear();
    usp_spotify_pkce::zeroize(code_.data(), code_.size());
    usp_spotify_pkce::zeroize(verifier_.data(), verifier_.size());
    clear_config_();
    pending_ = false;
    received_ms_ = 0;
  }

 private:
  void clear_config_() {
    usp_spotify_pkce::zeroize(client_id_.data(), client_id_.size());
    usp_spotify_pkce::zeroize(redirect_uri_.data(), redirect_uri_.size());
  }
  usp_spotify_pkce::Session session_;
  std::array<char, MAX_CODE + 1> code_{};
  std::array<char, usp_spotify_pkce::VERIFIER_CHARS + 1> verifier_{};
  std::array<char, usp_spotify_pkce::MAX_CLIENT_ID + 1> client_id_{};
  std::array<char, usp_spotify_pkce::MAX_REDIRECT_URI + 1> redirect_uri_{};
  bool pending_{false};
  uint32_t received_ms_{0};
};

static_assert(sizeof(Exchange) <= 2048, "Authorization inbox must remain bounded");
static_assert(sizeof(TokenRequest) <= 1536, "Token handoff must remain bounded");

}  // namespace usp_spotify_auth_return
