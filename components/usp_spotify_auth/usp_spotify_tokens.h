#pragma once
#include "usp_spotify_auth_return.h"
#include "control_scope.h"
#include <array>
#include <cstring>
#include <string_view>

namespace usp_spotify_tokens {
constexpr size_t MAX_TOKEN = 2048;
constexpr size_t MAX_BODY = 12288;
constexpr size_t MAX_FORM = 8192;
constexpr uint32_t MAX_LIFETIME_SECONDS = 86400;

/** Bearer values must be safe for a single HTTP Authorization header. */
inline bool bearer_value(std::string_view value) {
  if (value.empty() || value.size() > MAX_TOKEN) return false;
  bool padding = false;
  for (char ch : value) {
    if (ch == '=') { padding = true; continue; }
    if (padding) return false;
    if (!usp_spotify_pkce::ascii_alnum(ch) &&
        std::string_view("-._~+/").find(ch) == std::string_view::npos) return false;
  }
  return value.front() != '=';
}

/** Refresh values are opaque, bounded printable bytes, encoded in a POST body. */
inline bool opaque_value(std::string_view value) {
  if (value.empty() || value.size() > MAX_TOKEN) return false;
  for (unsigned char ch : value) if (ch < 0x21 || ch > 0x7e) return false;
  return true;
}

/** A corrupted unterminated field is rejected before constructing a view. */
template<size_t N>
inline std::string_view bounded(const std::array<char, N> &value) {
  const auto end = std::find(value.begin(), value.end(), char(0));
  if (end == value.end()) return {};
  return {value.data(), size_t(end - value.begin())};
}

/** Temporary parser output is never copyable and erases all sensitive fields. */
struct Reply {
  std::array<char, MAX_TOKEN + 1> access{}, refresh{};
  std::array<char, 513> scope{};
  std::array<char, 129> error{};
  uint32_t expires_seconds{0};
  uint8_t fields{0};
  Reply() = default;
  Reply(const Reply &) = delete;
  Reply &operator=(const Reply &) = delete;
  ~Reply() { this->clear(); }
  void clear() {
    usp_spotify_pkce::zeroize(access.data(), access.size());
    usp_spotify_pkce::zeroize(refresh.data(), refresh.size());
    usp_spotify_pkce::zeroize(scope.data(), scope.size());
    usp_spotify_pkce::zeroize(error.data(), error.size());
    expires_seconds = 0; fields = 0;
  }
  bool success() const {
    return (fields & 7U) == 7U && !(fields & 96U) &&
        bearer_value(bounded(access)) && expires_seconds &&
        expires_seconds <= MAX_LIFETIME_SECONDS;
  }
};

/** Flat provider reply parser rejects duplicate decoded keys and trailing data. */
class ReplyParser {
 public:
  explicit ReplyParser(std::string_view text) : text_(text) {}
  ~ReplyParser() { usp_spotify_pkce::zeroize(value_bytes_.data(), value_bytes_.size()); }
  bool parse(Reply *output) {
    if (!output) return false;
    output->clear();
    if (text_.empty() || text_.size() > MAX_BODY || !this->take_('{')) return false;
    while (true) {
      std::array<char, 32> key{};
      if (!this->string_(key.data(), key.size()) || !this->take_(':')) break;
      const uint8_t field = this->field_(key.data());
      if (!field || (output->fields & field)) break;
      output->fields |= field;
      if (!this->value_(field, output)) break;
      if (this->take_('}')) {
        this->space_();
        if (at_ == text_.size() &&
            (output->success() || this->error_shape_(*output))) return true;
        break;
      }
      if (!this->take_(',')) break;
    }
    output->clear(); return false;
  }

 private:
  void space_() {
    while (at_ < text_.size() &&
           std::string_view(" \t\r\n").find(text_[at_]) != std::string_view::npos) ++at_;
  }
  bool take_(char expected) {
    this->space_();
    if (at_ == text_.size() || text_[at_] != expected) return false;
    ++at_; return true;
  }
  static int hex_(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
  }
  bool escaped_(char *value) {
    if (at_ == text_.size()) return false;
    const char ch = text_[at_++];
    if (ch == '"' || ch == '\\' || ch == '/') { *value = ch; return true; }
    if (ch != 'u' || text_.size() - at_ < 4) return false;
    uint32_t point = 0;
    for (size_t i = 0; i < 4; ++i) {
      const int digit = this->hex_(text_[at_++]);
      if (digit < 0) return false;
      point = (point << 4U) | uint32_t(digit);
    }
    if (point < 0x20 || point > 0x7e) return false;
    *value = char(point); return true;
  }
  bool string_(char *output, size_t capacity) {
    if (!capacity || !this->take_('"')) return false;
    size_t length = 0;
    while (at_ < text_.size()) {
      char ch = text_[at_++];
      if (ch == '"') { output[length] = 0; return true; }
      if (ch == '\\' && !this->escaped_(&ch)) return false;
      if (uint8_t(ch) < 0x20 || uint8_t(ch) > 0x7e || length >= capacity - 1)
        return false;
      output[length++] = ch;
    }
    return false;
  }
  static uint8_t field_(std::string_view key) {
    if (key == "access_token") return 1;
    if (key == "token_type") return 2;
    if (key == "expires_in") return 4;
    if (key == "refresh_token") return 8;
    if (key == "scope") return 16;
    if (key == "error") return 32;
    if (key == "error_description") return 64;
    return 0;
  }
  bool integer_(uint32_t *output) {
    this->space_();
    const size_t first = at_;
    uint32_t number = 0;
    while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') {
      if (at_ - first >= 6 || (at_ > first && text_[first] == '0')) return false;
      number = number * 10U + uint32_t(text_[at_++] - '0');
      if (number > MAX_LIFETIME_SECONDS) return false;
    }
    if (at_ == first || number == 0) return false;
    *output = number; return true;
  }
  bool value_(uint8_t field, Reply *output) {
    if (field == 4) return this->integer_(&output->expires_seconds);
    value_bytes_.fill(0);
    if (!this->string_(value_bytes_.data(), value_bytes_.size())) return false;
    const std::string_view value(value_bytes_.data());
    if (field == 1)
      return bearer_value(value) && usp_spotify_auth_return::copy_field(output->access, value);
    if (field == 2) return value == "Bearer";
    if (field == 8)
      return opaque_value(value) && usp_spotify_auth_return::copy_field(output->refresh, value);
    if (field == 16) return usp_spotify_auth_return::copy_field(output->scope, value);
    if (field == 32)
      return !value.empty() && usp_spotify_auth_return::copy_field(output->error, value);
    return field == 64;
  }
  static bool error_shape_(const Reply &reply) {
    return (reply.fields & 32U) && !(reply.fields & 31U) && reply.error[0];
  }
  std::string_view text_;
  size_t at_{0};
  std::array<char, MAX_TOKEN + 1> value_bytes_{};
};

/** Fixed form buffer never places raw tokens in a URL or grows the heap. */
class Form {
 public:
  Form() = default;
  Form(const Form &) = delete;
  Form &operator=(const Form &) = delete;
  ~Form() { this->clear(); }
  void clear() {
    usp_spotify_pkce::zeroize(bytes_.data(), bytes_.size()); size_ = 0;
  }
  bool append(std::string_view name, std::string_view value) {
    if (size_ && !this->byte_('&')) return this->reject_();
    if (!this->encode_(name) || !this->byte_('=') || !this->encode_(value))
      return this->reject_();
    return true;
  }
  const char *data() const { return bytes_.data(); }
  size_t size() const { return size_; }

 private:
  bool reject_() { this->clear(); return false; }
  bool byte_(char ch) {
    if (size_ >= bytes_.size() - 1) return false;
    bytes_[size_++] = ch; bytes_[size_] = 0; return true;
  }
  bool encode_(std::string_view value) {
    constexpr char HEX[] = "0123456789ABCDEF";
    for (unsigned char ch : value) {
      if (usp_spotify_pkce::ascii_alnum(char(ch)) ||
          std::string_view("-._~").find(char(ch)) != std::string_view::npos) {
        if (!this->byte_(char(ch))) return false;
      } else if (!this->byte_('%') || !this->byte_(HEX[ch >> 4U]) ||
                 !this->byte_(HEX[ch & 15U])) return false;
    }
    return true;
  }
  std::array<char, MAX_FORM> bytes_{};
  size_t size_{0};
};

/** A code is encoded only once by its owner; this builder performs no I/O. */
inline bool code_form(const usp_spotify_auth_return::TokenRequest &request, Form *output) {
  if (!output) return false;
  output->clear();
  const auto code = bounded(request.code), verifier = bounded(request.verifier);
  if (code.empty() || !usp_spotify_auth_return::safe_field(code, usp_spotify_auth_return::MAX_CODE) ||
      verifier.size() != usp_spotify_pkce::VERIFIER_CHARS ||
      !usp_spotify_pkce::valid_client_id(bounded(request.client_id)) ||
      !usp_spotify_auth_return::valid_redirect(bounded(request.redirect_uri))) return false;
  for (char ch : verifier)
    if (!usp_spotify_pkce::ascii_alnum(ch) && ch != '-' && ch != '_') return false;
  return output->append("grant_type", "authorization_code") &&
      output->append("client_id", bounded(request.client_id)) &&
      output->append("code", code) && output->append("redirect_uri", bounded(request.redirect_uri)) &&
      output->append("code_verifier", verifier);
}

inline bool refresh_form(std::string_view client, std::string_view refresh, Form *output) {
  if (!output) return false;
  output->clear();
  if (!usp_spotify_pkce::valid_client_id(client) || !opaque_value(refresh)) return false;
  return output->append("grant_type", "refresh_token") &&
      output->append("client_id", client) && output->append("refresh_token", refresh);
}

/** Only the future worker may access this RAM store; caller owns synchronization. */
class Store {
 public:
  Store() = default;
  Store(const Store &) = delete;
  Store &operator=(const Store &) = delete;
  ~Store() { this->clear(); }
  bool install(const Reply &reply, std::string_view client, uint32_t started,
               uint32_t completed, bool refreshing) {
    if (!reply.success() || !usp_spotify_pkce::valid_client_id(client)) return false;
    if (refreshing && (!active_ || client != client_.data())) return false;
    if ((!refreshing && !opaque_value(bounded(reply.refresh))) ||
        (reply.refresh[0] && !opaque_value(bounded(reply.refresh)))) return false;
    const uint32_t lifetime = reply.expires_seconds * 1000U;
    if (uint32_t(completed - started) >= lifetime) return false;
    std::array<char, usp_spotify_pkce::MAX_CLIENT_ID + 1> copied_client{};
    usp_spotify_auth_return::copy_field(copied_client, client);
    if (!refreshing) this->clear();
    usp_spotify_auth_return::copy_field(access_, bounded(reply.access));
    if (reply.refresh[0]) usp_spotify_auth_return::copy_field(refresh_, bounded(reply.refresh));
    if (!refreshing || (reply.fields & 16U))
      usp_spotify_auth_return::copy_field(scope_, bounded(reply.scope));
    usp_spotify_auth_return::copy_field(client_, bounded(copied_client));
    issued_ = started; lifetime_ = lifetime; active_ = true;
    return true;
  }
  /** Restore only long-lived refresh metadata; access remains empty until refresh succeeds. */
  bool restore(std::string_view refresh, std::string_view client,
               std::string_view scope, uint32_t now) {
    if (!opaque_value(refresh) || !usp_spotify_pkce::valid_client_id(client) ||
        scope.size() > scope_.size() - 1) return false;
    for (unsigned char ch : scope) if (ch < 0x20 || ch > 0x7e) return false;
    this->clear();
    if (!usp_spotify_auth_return::copy_field(refresh_, refresh) ||
        !usp_spotify_auth_return::copy_field(client_, client) ||
        !usp_spotify_auth_return::copy_field(scope_, scope)) {
      this->clear(); return false;
    }
    issued_ = now; lifetime_ = 0; active_ = true; return true;
  }

  bool valid(uint32_t now) const {
    return active_ && lifetime_ && uint32_t(now - issued_) < lifetime_;
  }
  bool refresh_due(uint32_t now) const {
    if (!active_ || !refresh_[0]) return false;
    const uint32_t age = now - issued_, margin = std::min<uint32_t>(60000U, lifetime_ / 4U);
    return age >= lifetime_ || lifetime_ - age <= margin;
  }
  std::string_view access(uint32_t now) const { return valid(now) ? access_.data() : ""; }
  std::string_view refresh() const { return active_ ? refresh_.data() : ""; }
  std::string_view client() const { return active_ ? client_.data() : ""; }
  std::string_view scope() const { return active_ ? scope_.data() : ""; }
  /** Only actual granted scope may authorize controls, and only with a valid token. */
  bool has_modify_scope(uint32_t now) const {
    return valid(now) && usp_spotify_controls::has_scope(
        scope(),"user-modify-playback-state");
  }

  void clear() {
    usp_spotify_pkce::zeroize(access_.data(), access_.size());
    usp_spotify_pkce::zeroize(refresh_.data(), refresh_.size());
    usp_spotify_pkce::zeroize(scope_.data(), scope_.size());
    usp_spotify_pkce::zeroize(client_.data(), client_.size());
    active_ = false; issued_ = lifetime_ = 0;
  }

 private:
  std::array<char, MAX_TOKEN + 1> access_{}, refresh_{};
  std::array<char, 513> scope_{};
  std::array<char, 129> client_{};
  uint32_t issued_{0}, lifetime_{0};
  bool active_{false};
};
static_assert(sizeof(Reply) <= 4800 && sizeof(Store) <= 4800, "Token state must stay bounded");
static_assert(sizeof(Form) <= 8208 && sizeof(ReplyParser) <= 2100, "Token scratch must stay bounded");
}  // namespace usp_spotify_tokens
