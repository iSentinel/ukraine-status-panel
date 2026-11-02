#pragma once
namespace esphome {
namespace setup_priority { constexpr float DATA = 600; }
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 0; }
  void status_clear_warning() { warning = false; }
  void status_set_warning(const char *) { warning = true; }
  bool warning{false};
};
}
