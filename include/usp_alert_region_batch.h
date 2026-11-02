#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#include "usp_alert_region_colors.h"

namespace usp_alert_region_batch {

inline bool supported_state(uint8_t state) {
  switch (state) {
    case 0:
    case 1:
    case 2:
    case 4:
    case 8:
    case 16:
    case 32:
    case 64:
    case 128:
      return true;
    default:
      return false;
  }
}

template<typename Setter>
inline void render(size_t base, size_t count, const uint8_t *desired_states,
                   uint32_t dirty_mask, uint8_t *cache_states,
                   uint32_t &cache_valid_mask, uint32_t &inflight_mask,
                   uint8_t &changed_count, uint8_t palette, Setter &&setter) {
  for (size_t offset = 0; offset < count; ++offset) {
    const size_t index = base + offset;
    const uint32_t bit = 1UL << index;
    const uint8_t state = desired_states[index];
    if ((dirty_mask & bit) != 0 && supported_state(state)) {
      cache_states[index] = state;
      cache_valid_mask |= bit;
      inflight_mask |= bit;
      ++changed_count;
      setter(offset, usp_alert_region_colors::color_hex(state, palette));
    }
    inflight_mask &= ~bit;
  }
}

}  // namespace usp_alert_region_batch
