#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace esphome::web_server_idf::digest_auth {

static constexpr size_t TOKEN_HEX_LENGTH = 32;
static constexpr size_t NONCE_CAPACITY = 12;
static constexpr size_t PENDING_NONCE_CAPACITY = 2;
static_assert(PENDING_NONCE_CAPACITY < NONCE_CAPACITY);
static constexpr uint32_t NONCE_TTL_MS = 5U * 60U * 1000U;
static constexpr size_t NC_WINDOW_BITS = 64;

inline bool parse_nonce_count(std::string_view value, uint32_t &out) {
  if (value.size() != 8)
    return false;
  uint32_t result = 0;
  for (char ch : value) {
    uint8_t digit;
    if (ch >= '0' && ch <= '9')
      digit = static_cast<uint8_t>(ch - '0');
    else if (ch >= 'a' && ch <= 'f')
      digit = static_cast<uint8_t>(ch - 'a' + 10);
    else if (ch >= 'A' && ch <= 'F')
      digit = static_cast<uint8_t>(ch - 'A' + 10);
    else
      return false;
    result = (result << 4U) | digit;
  }
  if (result == 0)
    return false;
  out = result;
  return true;
}

struct NonceRecord {
  std::array<char, TOKEN_HEX_LENGTH + 1> nonce{};
  std::array<char, TOKEN_HEX_LENGTH + 1> opaque{};
  uint32_t issued_ms{};
  uint32_t highest_nc{};
  uint64_t seen_nc_mask{};
  bool active{};
};

enum class NonceStatus { ACCEPTED, EXPIRED, UNKNOWN, OPAQUE_MISMATCH, REPLAY };

class NonceStore {
 public:
  /** Challenges reuse only pending slots, preserving authenticated clients. */
  void issue(const char *nonce, const char *opaque, uint32_t now) {
    auto &record = this->records_[this->next_];
    this->next_ = (this->next_ + 1U) % PENDING_NONCE_CAPACITY;
    record = NonceRecord{};
    std::memcpy(record.nonce.data(), nonce, TOKEN_HEX_LENGTH);
    std::memcpy(record.opaque.data(), opaque, TOKEN_HEX_LENGTH);
    record.issued_ms = now;
    record.active = true;
  }

  /** Only a validated new count renews the idle lifetime. */
  NonceStatus consume_result(std::string_view nonce, std::string_view opaque,
                             uint32_t nc, uint32_t now) {
    bool known_nonce = false;
    for (size_t index = 0; index < this->records_.size(); ++index) {
      auto &record = this->records_[index];
      if (!record.active || !token_equals_(record.nonce, nonce)) continue;
      known_nonce = true;
      if (!token_equals_(record.opaque, opaque)) continue;
      if (static_cast<uint32_t>(now - record.issued_ms) >= NONCE_TTL_MS) {
        record.active = false;
        return NonceStatus::EXPIRED;
      }
      if (!mark_nonce_count_(record, nc)) return NonceStatus::REPLAY;
      record.issued_ms = now;
      if (index < PENDING_NONCE_CAPACITY) {
        this->records_[this->session_slot_(now)] = record;
        record.active = false;
      }
      return NonceStatus::ACCEPTED;
    }
    return known_nonce ? NonceStatus::OPAQUE_MISMATCH : NonceStatus::UNKNOWN;
  }

  bool consume(std::string_view nonce, std::string_view opaque,
               uint32_t nc, uint32_t now) {
    return this->consume_result(nonce, opaque, nc, now) == NonceStatus::ACCEPTED;
  }

 private:
  /** New valid logins may replace only the least recently used valid session. */
  size_t session_slot_(uint32_t now) const {
    size_t oldest = PENDING_NONCE_CAPACITY;
    uint32_t maximum_age = 0;
    for (size_t index = PENDING_NONCE_CAPACITY; index < this->records_.size(); ++index) {
      const auto &record = this->records_[index];
      const uint32_t age = now - record.issued_ms;
      if (!record.active || age >= NONCE_TTL_MS) return index;
      if (age > maximum_age) {
        maximum_age = age;
        oldest = index;
      }
    }
    return oldest;
  }

  static bool token_equals_(
      const std::array<char, TOKEN_HEX_LENGTH + 1> &stored,
      std::string_view supplied) {
    return supplied.size() == TOKEN_HEX_LENGTH &&
           std::memcmp(stored.data(), supplied.data(), TOKEN_HEX_LENGTH) == 0;
  }

  static bool mark_nonce_count_(NonceRecord &record, uint32_t nc) {
    if (nc == 0)
      return false;
    if (record.highest_nc == 0) {
      record.highest_nc = nc;
      record.seen_nc_mask = 1;
      return true;
    }
    if (nc > record.highest_nc) {
      const uint32_t advance = nc - record.highest_nc;
      record.seen_nc_mask =
          advance >= NC_WINDOW_BITS ? 1ULL
                                    : (record.seen_nc_mask << advance) | 1ULL;
      record.highest_nc = nc;
      return true;
    }
    const uint32_t age = record.highest_nc - nc;
    if (age >= NC_WINDOW_BITS)
      return false;
    const uint64_t bit = 1ULL << age;
    if ((record.seen_nc_mask & bit) != 0)
      return false;
    record.seen_nc_mask |= bit;
    return true;
  }

  std::array<NonceRecord, NONCE_CAPACITY> records_{};
  size_t next_{};
};

}  // namespace esphome::web_server_idf::digest_auth
