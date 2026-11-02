#pragma once
#include <cstdint>
namespace esphome {
inline uint32_t test_millis = 100;
inline uint32_t millis() { return test_millis; }
constexpr uint32_t fnv1a_hash(const char *) { return 123; }
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  void mark_failed() {}
};
}
