#pragma once
#include <cstdint>

namespace usp_confirmation {

/** Require two completed clicks within ten seconds; never save this state. */
class Confirmation {
 public:
  bool activate(uint32_t now) {
    if (pending(now)) {
      clear();
      return true;
    }
    started_ = now;
    armed_ = true;
    return false;
  }

  bool pending(uint32_t now) const {
    return armed_ && static_cast<uint32_t>(now - started_) < 10000U;
  }

  void clear() { armed_ = false; }

 private:
  uint32_t started_{0};
  bool armed_{false};
};

/** Use one confirmation state for the physical restart control. */
inline Confirmation &restart_request() {
  static Confirmation request;
  return request;
}

}  // namespace usp_confirmation
