#include "secret_text.h"

#include <cstring>
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::usp_secret_text {

static const char *const TAG = "usp.secret_text";

/** Retain the exact key used by the previous TemplateTextSaver<64>. */
uint32_t SecretText::preference_key_() {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  uint32_t key = this->get_preference_hash();
#pragma GCC diagnostic pop
  key += this->traits.get_min_length() << 2;
  key += this->traits.get_max_length() << 4;
  key += fnv1_hash(this->traits.get_pattern_c_str()) << 6;
  return key;
}

/** Restore only into private storage; never publish the restored secret. */
void SecretText::setup() {
  if (this->restore_value_) {
    this->preference_ =
        global_preferences->make_preference<uint8_t[65]>(this->preference_key_());
    uint8_t storage[65]{};
    if (this->preference_.load(&storage)) {
      if (storage[0] <= 64U)
        this->secret_.assign(reinterpret_cast<char *>(storage + 1), storage[0]);
      else
        ESP_LOGW(TAG, "Stored secret length is invalid");
    }
    this->previous_ = this->secret_;
  }
  this->publish_state("");
}

/** Save a zero-filled legacy length-prefixed record only when it changed. */
void SecretText::save_() {
  if (!this->restore_value_ || this->secret_ == this->previous_) return;
  uint8_t storage[65]{};
  storage[0] = static_cast<uint8_t>(this->secret_.size());
  std::memcpy(storage + 1, this->secret_.data(), this->secret_.size());
  if (!this->preference_.save(&storage)) {
    this->status_set_warning("Secret could not be saved");
    ESP_LOGW(TAG, "Secret could not be saved");
    return;
  }
  this->previous_ = this->secret_;
  this->status_clear_warning();
}

/** Deliver the value internally and publish only an empty state to clients. */
void SecretText::control(const std::string &value) {
  if (value.size() > 64U) {
    ESP_LOGW(TAG, "Secret input is too long");
    return;
  }
  this->secret_ = value;
  this->set_trigger_.trigger(value);
  this->save_();
  this->publish_state("");
}

/** Describe the field without including its value or a credential prefix. */
void SecretText::dump_config() {
  LOG_TEXT("", "Write-only password input", this);
  ESP_LOGCONFIG(TAG, "  Restore value: %s", YESNO(this->restore_value_));
}

}  // namespace esphome::usp_secret_text
