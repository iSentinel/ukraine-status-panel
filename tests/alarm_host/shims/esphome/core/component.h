#pragma once
namespace esphome {
namespace setup_priority { constexpr float DATA = 600; }
class PollingComponent {
 public:
  virtual ~PollingComponent() = default;
  virtual void setup() {}
  virtual void update() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 0; }
  void status_clear_warning() {}
  void status_set_warning(const char *) {}
};
}
