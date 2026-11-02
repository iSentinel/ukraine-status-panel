
#pragma once
#include <cstdint>
namespace esphome {
extern uint32_t host_millis;
inline uint32_t millis(){return host_millis;}
namespace setup_priority { static constexpr float DATA=600.0f; }
class Component {
 public:
  virtual ~Component()=default;
  virtual void setup(){}
  virtual void loop(){}
  virtual void on_shutdown(){}
  virtual void dump_config(){}
  virtual float get_setup_priority() const {return 0;}
  void status_set_warning(const char*){}
  void status_clear_warning(){}
};
}
