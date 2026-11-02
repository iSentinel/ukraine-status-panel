#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "usp_map_touch_0_115_49.h"
#include "usp_alert_region_ids.h"

namespace usp_alert_rows {

inline const char *kind(int state, uint8_t language) {
  if (state == 8) {
    if (language == 0) return "Повітряна загроза";
    if (language == 1) return "Воздушная угроза";
    return "Air threat";
  }
  if (state == 64) {
    if (language == 0) return "Балістика";
    if (language == 1) return "Баллистика";
    return "Ballistic";
  }
  if (state == 128) {
    if (language == 0 || language == 1) return "КАБ";
    return "Guided bomb";
  }
  if (state == 4) {
    if (language == 0) return "Ракети";
    if (language == 1) return "Ракеты";
    return "Missile";
  }
  if (state == 32) {
    if (language == 0) return "Вибухи";
    if (language == 1) return "Взрывы";
    return "Explosion";
  }
  if (state == 16) {
    if (language == 0) return "Тривога";
    if (language == 1) return "Тревога";
    return "Air alert";
  }
  return language == 2 ? "Drone" : "БПЛА";
}

inline std::string empty(uint8_t language) {
  if (language == 0) return "Немає активних тривог";
  if (language == 1) return "Нет активных тревог";
  return "No active alerts";
}

// Alert renderer states are indexed in ISO-3166-2 mask order (UA-05,
// UA-07, ...). region_name() uses the panel's internal region IDs instead.
// Keep this translation identical to select_region_from_map_point.
inline std::string row(
    const int *states, size_t state_count, size_t target,
    uint8_t language) {
  const size_t limit = state_count < 25 ? state_count : 25;
  size_t ordinal = 0;
  for (size_t index = 0; index < limit; ++index) {
    const int state = states[index];
    if (state == 0 || state == 1) continue;
    if (ordinal++ != target) continue;
    return std::string(usp_map_touch::region_name(
               region_id_for_index(index), language)) +
           " · " + kind(state, language);
  }
  return target == 0 ? empty(language) : std::string();
}

}  // namespace usp_alert_rows
