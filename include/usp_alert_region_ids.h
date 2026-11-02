#pragma once
#include <cstddef>
#include <cstdint>

namespace usp_alert_rows {
inline constexpr uint16_t REGION_IDS[25] = {
    4, 8, 16, 9, 28, 10, 11, 12, 13, 14, 15, 9999, 27,
    17, 18, 19, 5, 20, 21, 22, 23, 3, 24, 25, 26};

inline uint16_t region_id_for_index(size_t index) {
  return index < 25 ? REGION_IDS[index] : 0;
}

}  // namespace usp_alert_rows
