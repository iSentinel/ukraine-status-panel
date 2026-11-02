#pragma once

#include <cstdint>

namespace usp_room_period {

enum class Period : uint8_t {
  HOURS_24 = 0,
  DAYS_7 = 1,
  DAYS_30 = 2,
  YEAR_1 = 3,
};

constexpr Period sanitize(unsigned value) {
  return value <= static_cast<unsigned>(Period::YEAR_1)
      ? static_cast<Period>(value)
      : Period::HOURS_24;
}

constexpr bool uses_aggregate(Period period) {
  return period != Period::HOURS_24;
}

}  // namespace usp_room_period
