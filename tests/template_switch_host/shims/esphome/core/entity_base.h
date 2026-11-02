#pragma once
#include <string>
#include "preferences.h"
namespace esphome {
class EntityBase {
 public:
  virtual ~EntityBase() = default;
  const std::string &get_name() const { return name_; }
  void set_has_state(bool) {}
  template<typename T> ESPPreferenceObject make_entity_preference() {
    return {};
  }
 protected:
  std::string name_{"Alarm Sound Enabled"};
};
}
