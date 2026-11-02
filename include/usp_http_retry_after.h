#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace usp_http_retry_after {

static constexpr uint32_t MAX_RETRY_AFTER_MS = 900000U;
static constexpr uint32_t MIN_RETRY_AFTER_MS = 1000U;

struct ParsedDelay {
  bool valid{false};
  uint32_t milliseconds{0};
};

inline bool ascii_iequal(const char *left, const char *right) {
  if (left == nullptr || right == nullptr) return false;
  while (*left != '\0' && *right != '\0') {
    unsigned char a = static_cast<unsigned char>(*left++);
    unsigned char b = static_cast<unsigned char>(*right++);
    if (a >= 'A' && a <= 'Z') a = static_cast<unsigned char>(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = static_cast<unsigned char>(b - 'A' + 'a');
    if (a != b) return false;
  }
  return *left == '\0' && *right == '\0';
}

inline ParsedDelay parse_delta_seconds(const char *raw) {
  if (raw == nullptr) return {};
  const char *begin = raw;
  while (*begin == ' ' || *begin == '\t') ++begin;
  const char *end = begin + std::strlen(begin);
  while (end != begin && (end[-1] == ' ' || end[-1] == '\t')) --end;
  if (begin == end) return {};

  const uint64_t cap_seconds = MAX_RETRY_AFTER_MS / 1000U;
  uint64_t seconds = 0;
  for (const char *cursor = begin; cursor != end; ++cursor) {
    if (*cursor < '0' || *cursor > '9') return {};
    if (seconds <= cap_seconds) {
      seconds = std::min<uint64_t>(
          seconds * 10U + static_cast<uint64_t>(*cursor - '0'),
          cap_seconds + 1U);
    }
  }
  const uint64_t raw_ms = seconds * 1000U;
  const uint32_t bounded = static_cast<uint32_t>(std::min<uint64_t>(
      std::max<uint64_t>(raw_ms, MIN_RETRY_AFTER_MS), MAX_RETRY_AFTER_MS));
  return {true, bounded};
}

struct HeaderCapture {
  bool valid{false};
  uint32_t delay_ms{0};

  void reset() {
    this->valid = false;
    this->delay_ms = 0;
  }

  void observe(const char *key, const char *value) {
    if (!ascii_iequal(key, "Retry-After")) return;
    const ParsedDelay parsed = parse_delta_seconds(value);
    if (!parsed.valid) return;
    this->delay_ms = this->valid ? std::max(this->delay_ms, parsed.milliseconds)
                                 : parsed.milliseconds;
    this->valid = true;
  }
};

inline uint32_t selected_delay(int status, const HeaderCapture &header,
                               uint32_t fallback_ms) {
  const uint32_t fallback = std::min(fallback_ms, MAX_RETRY_AFTER_MS);
  if (status != 429 || !header.valid) return fallback;
  return std::max(fallback, header.delay_ms);
}

inline bool time_reached(uint32_t now, uint32_t target) {
  return static_cast<int32_t>(now - target) >= 0;
}

struct Gate {
  bool active{false};
  uint32_t until_ms{0};

  bool ready(uint32_t now) {
    if (this->active && time_reached(now, this->until_ms)) {
      this->active = false;
    }
    return !this->active;
  }

  uint32_t apply(int status, const HeaderCapture &header,
                 uint32_t fallback_ms, uint32_t now) {
    if (status != 429) return 0;
    const uint32_t delay = selected_delay(status, header, fallback_ms);
    this->active = true;
    this->until_ms = now + delay;
    return delay;
  }
};

static_assert(sizeof(HeaderCapture) <= 8U, "Retry-After capture must stay bounded");
static_assert(sizeof(Gate) <= 8U, "Retry-After gate must stay bounded");

}  // namespace usp_http_retry_after
