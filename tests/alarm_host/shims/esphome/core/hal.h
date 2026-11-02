#pragma once
#include <cstdint>
extern uint32_t alarm_test_millis;
namespace esphome {
inline uint32_t millis() { return alarm_test_millis; }
}
