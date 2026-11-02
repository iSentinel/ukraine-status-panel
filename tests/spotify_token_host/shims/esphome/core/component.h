#pragma once
#include <atomic>
#include <cstdint>
#include <thread>
#include <chrono>
namespace esphome {
inline std::atomic<uint32_t> host_time{0};
void host_release_tasks();
inline void delay(unsigned value) {host_release_tasks();std::this_thread::sleep_for(std::chrono::milliseconds(value));}
inline uint32_t millis() { return host_time.load(); }
class Component {
 public:
  virtual ~Component() = default;
  virtual void on_shutdown() {}
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  void mark_failed() { failed_ = true; }
  bool is_failed() const { return failed_; }
 private:
  bool failed_{false};
};
}
