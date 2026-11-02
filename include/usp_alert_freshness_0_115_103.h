#pragma once

#include <cstdint>

namespace usp_alert_freshness {

enum class Decision : uint8_t {
  KEEP = 0,
  STALE = 1,
  NO_DATA = 2,
};

inline Decision decide(
    bool has_data,
    uint8_t live_state,
    bool transport_connected,
    uint32_t transport_age_ms,
    uint32_t data_age_ms,
    uint32_t stale_timeout_ms,
    uint32_t nodata_timeout_ms) {
  if (!has_data || live_state == 0) {
    return Decision::KEEP;
  }

  if (data_age_ms >= nodata_timeout_ms ||
      transport_age_ms >= nodata_timeout_ms) {
    return Decision::NO_DATA;
  }

  if (live_state == 1 &&
      (!transport_connected || data_age_ms >= stale_timeout_ms ||
       transport_age_ms >= stale_timeout_ms)) {
    return Decision::STALE;
  }

  return Decision::KEEP;
}

}  // namespace usp_alert_freshness
