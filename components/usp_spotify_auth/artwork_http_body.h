#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace usp_spotify_artwork_http {
constexpr size_t MAX_BODY = 128 * 1024;
constexpr uint32_t DEADLINE = 8000;
/** ASCII field comparison; provider text is never copied to logs. */
inline bool equal(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    char ch = a[i];
    if (ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a');
    if (ch != b[i]) return false;
  }
  return true;
}
inline std::string_view trim(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
    text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
    text.remove_suffix(1);
  return text;
}
/** Parse an unsigned bounded decimal without allowing overflow. */
inline bool decimal(std::string_view text, uint32_t limit, uint32_t &value) {
  text = trim(text);
  if (text.empty() || text.size() > 10) return false;
  value = 0;
  for (char ch : text) {
    if (ch < '0' || ch > '9') return false;
    const uint32_t digit = uint32_t(ch - '0');
    if (value > limit / 10 || (value == limit / 10 && digit > limit % 10))
      return false;
    value = value * 10 + digit;
  }
  return true;
}
/** Full provider cooldown is independent of the ordinary request retry delay. */
struct Cooldown {
  uint32_t delay{30000};
  bool explicit_delay{false};
  bool blocked{false};
  void header(std::string_view value) {
    uint32_t seconds = 0;
    if (!decimal(value, 86400, seconds)) { blocked = true; return; }
    const uint32_t duration = std::max<uint32_t>(1000, seconds * 1000);
    delay = explicit_delay ? std::max(delay, duration) : duration;
    explicit_delay = true;
  }
};

/** Bounded response collection only. TLS, origin and redirect checks are external. */
class Collector {
 public:
  Collector(uint8_t *buffer, size_t capacity, uint32_t now)
      : buffer_(buffer), capacity_(capacity), started_(now),
        valid_(buffer && capacity >= 4 && capacity <= MAX_BODY) {}
  bool header(std::string_view key, std::string_view value) {
    if (equal(key, "retry-after")) { cooldown.header(value); return valid_; }
    const bool length = equal(key, "content-length");
    const bool type = equal(key, "content-type");
    const bool encoding = equal(key, "content-encoding");
    if (!length && !type && !encoding) return valid_;
    if (value.size() > 128) { valid_ = false; return false; }
    value = trim(value);
    if (type && !equal(value, "image/jpeg")) valid_ = false;
    if (type && valid_) jpeg_ = true;
    if (encoding && !equal(value, "identity")) valid_ = false;
    if (length) {
      uint32_t parsed = 0;
      if (!decimal(value, uint32_t(capacity_), parsed) ||
          (has_length_ && parsed != declared_)) valid_ = false;
      declared_ = parsed;
      has_length_ = true;
    }
    return valid_;
  }
  bool append(const void *source, size_t count, uint32_t now) {
    if (!valid_ || uint32_t(now - started_) >= DEADLINE ||
        (count && !source) || size_ > capacity_ || count > capacity_ - size_) {
      valid_ = false;
      return false;
    }
    if (count) std::memcpy(buffer_ + size_, source, count);
    size_ += count;
    return true;
  }
  bool complete(int status, bool received_all, uint32_t now) const {
    return valid_ && jpeg_ && received_all && status == 200 && size_ >= 4 &&
        uint32_t(now - started_) < DEADLINE &&
        (!has_length_ || size_ == declared_);
  }
  size_t size() const { return size_; }
  Cooldown cooldown{};
 private:
  uint8_t *buffer_;
  size_t capacity_;
  uint32_t started_;
  size_t size_{0};
  uint32_t declared_{0};
  bool has_length_{false};
  bool valid_;
  bool jpeg_{false};
};
}  // namespace usp_spotify_artwork_http
