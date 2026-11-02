#include "ota_boot_activation_backend.h"

#include <esp_ota_ops.h>

namespace esphome::usp_ota_guard {

const char *OtaBootActivationBackend::state_name(BootActivationState state) {
  switch (state) {
    case BootActivationState::DISABLED: return "disabled";
    case BootActivationState::IDLE: return "idle";
    case BootActivationState::ACTIVATING: return "activating";
    case BootActivationState::ACTIVATED: return "activated";
    case BootActivationState::FAILED: return "failed";
  }
  return "unknown";
}

const char *OtaBootActivationBackend::failure_name(
    BootActivationFailure failure) {
  switch (failure) {
    case BootActivationFailure::NONE: return "none";
    case BootActivationFailure::DISABLED: return "disabled";
    case BootActivationFailure::INVALID_TARGET: return "invalid_target";
    case BootActivationFailure::NOT_APP_PARTITION: return "not_app_partition";
    case BootActivationFailure::TARGET_IS_RUNNING: return "target_is_running";
    case BootActivationFailure::SET_BOOT_FAILED: return "set_boot_failed";
    case BootActivationFailure::VERIFY_BOOT_FAILED: return "verify_boot_failed";
  }
  return "unknown";
}

void OtaBootActivationBackend::fail_(BootActivationFailure failure,
                                     esp_err_t error) {
  this->snapshot_.state = BootActivationState::FAILED;
  this->snapshot_.failure = failure;
  this->snapshot_.last_error = error;
  this->snapshot_.boot_selected = false;
}

bool OtaBootActivationBackend::select(const esp_partition_t *target) {
  this->snapshot_ = {};
  this->snapshot_.runtime_enabled = this->runtime_enabled_;
  if (!this->runtime_enabled_) {
    this->snapshot_.state = BootActivationState::DISABLED;
    this->snapshot_.failure = BootActivationFailure::DISABLED;
    this->snapshot_.last_error = ESP_ERR_NOT_SUPPORTED;
    return false;
  }

  const esp_partition_t *verified_target =
      target == nullptr ? nullptr : esp_partition_verify(target);
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (verified_target == nullptr || running == nullptr) {
    this->fail_(BootActivationFailure::INVALID_TARGET, ESP_ERR_INVALID_ARG);
    return false;
  }

  this->snapshot_.target_address = verified_target->address;
  this->snapshot_.running_address = running->address;
  if (verified_target->type != ESP_PARTITION_TYPE_APP) {
    this->fail_(BootActivationFailure::NOT_APP_PARTITION, ESP_ERR_INVALID_ARG);
    return false;
  }
  if (verified_target->address == running->address) {
    this->fail_(BootActivationFailure::TARGET_IS_RUNNING,
                ESP_ERR_INVALID_STATE);
    return false;
  }

  this->snapshot_.state = BootActivationState::ACTIVATING;
  const esp_err_t error = esp_ota_set_boot_partition(verified_target);
  if (error != ESP_OK) {
    this->fail_(BootActivationFailure::SET_BOOT_FAILED, error);
    return false;
  }

  const esp_partition_t *boot = esp_ota_get_boot_partition();
  if (boot == nullptr || boot->address != verified_target->address) {
    this->snapshot_.boot_address = boot == nullptr ? 0 : boot->address;
    this->fail_(BootActivationFailure::VERIFY_BOOT_FAILED,
                ESP_ERR_INVALID_STATE);
    return false;
  }

  this->snapshot_.boot_address = boot->address;
  this->snapshot_.state = BootActivationState::ACTIVATED;
  this->snapshot_.failure = BootActivationFailure::NONE;
  this->snapshot_.last_error = ESP_OK;
  this->snapshot_.boot_selected = true;
  return true;
}

}  // namespace esphome::usp_ota_guard
