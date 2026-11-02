#pragma once
#include <cstdint>
void alarm_test_event(uint8_t index);
namespace esphome {
template<typename... Args> class Trigger {
 public:
  void trigger(Args... args) {
    if constexpr (sizeof...(args) == 1) alarm_test_event(args...);
  }
};
}
