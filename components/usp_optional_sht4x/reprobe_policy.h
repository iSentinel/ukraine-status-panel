#pragma once

#include <cstdint>

namespace esphome::usp_optional_sht4x {

class ReprobePolicy {
 public:
  static constexpr uint32_t NEVER_SEEN_INTERVAL_MS = 300000U;
  static constexpr uint32_t RECONNECT_INTERVAL_MS = 60000U;

  bool due(uint32_t now) const {
    return !this->armed_ || uint32_t(now - this->due_ms_) < 0x80000000U;
  }

  void schedule(uint32_t now, bool ever_detected) {
    const uint32_t interval = ever_detected ? RECONNECT_INTERVAL_MS : NEVER_SEEN_INTERVAL_MS;
    this->due_ms_ = now + interval;
    this->armed_ = true;
  }

  void clear() { this->armed_ = false; }
  bool armed() const { return this->armed_; }

 protected:
  uint32_t due_ms_{0};
  bool armed_{false};
};

}  // namespace esphome::usp_optional_sht4x
