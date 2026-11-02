#pragma once

#include <string>
#include "esphome/components/text/text.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

namespace esphome::usp_secret_text {

/** A password setter with an empty public state and private internal value. */
class SecretText : public text::Text, public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }
  void set_restore_value(bool restore) { this->restore_value_ = restore; }
  Trigger<std::string> *get_set_trigger() { return &this->set_trigger_; }
  const std::string &secret_value() const { return this->secret_; }
  void clear_value() { this->control(""); }

 protected:
  void control(const std::string &value) override;
  uint32_t preference_key_();
  void save_();
  bool restore_value_{false};
  std::string secret_;
  std::string previous_;
  ESPPreferenceObject preference_;
  Trigger<std::string> set_trigger_;
};

}  // namespace esphome::usp_secret_text
