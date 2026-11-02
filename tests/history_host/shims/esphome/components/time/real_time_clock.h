#pragma once
#include <cstdint>
#include <ctime>
namespace esphome {
struct ESPTime {
  std::time_t timestamp{0};
  int hour{0}, minute{0}, day_of_month{0}, month{0};
  bool is_valid() const { return timestamp >= 1546300800; }
  static ESPTime from_epoch_local(uint32_t epoch) {
    std::time_t stamp = epoch;
    const auto *value = std::localtime(&stamp);
    return {stamp, value ? value->tm_hour : 0, value ? value->tm_min : 0,
            value ? value->tm_mday : 0, value ? value->tm_mon + 1 : 0};
  }
};
namespace time {
struct RealTimeClock {
  ESPTime value = ESPTime::from_epoch_local(1792962000);
  ESPTime now() const { return value; }
};
}
}
