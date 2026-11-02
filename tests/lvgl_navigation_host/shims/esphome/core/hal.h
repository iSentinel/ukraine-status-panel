#pragma once
#include <cstdint>
extern uint32_t navigation_test_tick;
namespace esphome {
inline uint32_t millis() { return navigation_test_tick; }
}
