#pragma once
#include <cstdint>
namespace esphome {
struct ESPTime {
  uint16_t year{2026};
  uint8_t month{10}, day_of_month{21}, hour{6}, minute{59}, day_of_week{4};
  uint32_t timestamp{1792555140};
  bool valid{true};
  bool is_valid() const { return valid; }
};
namespace time {
class RealTimeClock {
 public:
  ESPTime current{};
  ESPTime now() const { return current; }
};
}
}
