#pragma once
#include <cstdint>
namespace esphome {
inline uint32_t history_test_millis = 0;
inline uint32_t millis() { return history_test_millis; }
namespace setup_priority { constexpr float DATA = 600; }
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void on_shutdown() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 0; }
};
}
