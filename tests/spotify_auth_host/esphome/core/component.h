#pragma once
#include <atomic>
#include <cstdint>
namespace esphome {
inline std::atomic<uint32_t> host_time{0};
inline uint32_t millis() { return host_time.load(); }
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  void mark_failed() { failed_ = true; }
  bool is_failed() const { return failed_; }
 private:
  bool failed_{false};
};
}
