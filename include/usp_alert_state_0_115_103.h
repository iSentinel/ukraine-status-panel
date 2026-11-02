#pragma once

#include <cstdint>

namespace usp_alert_state {

// Convert a legacy threat bitmask into the single semantic state used by the
// renderer. Keep this priority identical everywhere the alert is presented.
inline uint8_t normalize(int raw) {
  if ((raw & 64) != 0) return 64;  // Explicit ballistic evidence has priority.
  if ((raw & 32) != 0) return 32;
  if ((raw & 4) != 0) return 4;
  if ((raw & 128) != 0) return 128;
  if ((raw & 2) != 0) return 2;
  if ((raw & 16) != 0) return 16;
  if ((raw & 8) != 0) return 8;
  if ((raw & 1) != 0) return 1;
  return 0;
}

}  // namespace usp_alert_state
