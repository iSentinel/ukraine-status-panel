#include "ota_rollback_confirm_backend.h"

namespace esphome::usp_ota_guard {

const char *OtaRollbackConfirmBackend::state_name(RollbackConfirmState state) {
  switch (state) {
    case RollbackConfirmState::DISABLED: return "disabled";
    case RollbackConfirmState::IDLE: return "idle";
    case RollbackConfirmState::CONFIRMING: return "confirming";
    case RollbackConfirmState::CONFIRMED: return "confirmed";
    case RollbackConfirmState::ALREADY_VALID: return "already_valid";
    case RollbackConfirmState::FAILED: return "failed";
  }
  return "unknown";
}

const char *OtaRollbackConfirmBackend::failure_name(
    RollbackConfirmFailure failure) {
  switch (failure) {
    case RollbackConfirmFailure::NONE: return "none";
    case RollbackConfirmFailure::DISABLED: return "disabled";
    case RollbackConfirmFailure::INVALID_RUNNING_PARTITION:
      return "invalid_running_partition";
    case RollbackConfirmFailure::RUNNING_NOT_APP: return "running_not_app";
    case RollbackConfirmFailure::BOOT_PARTITION_MISMATCH:
      return "boot_partition_mismatch";
    case RollbackConfirmFailure::STATE_QUERY_FAILED:
      return "state_query_failed";
    case RollbackConfirmFailure::STATE_NOT_PENDING_VERIFY:
      return "state_not_pending_verify";
    case RollbackConfirmFailure::MARK_VALID_FAILED:
      return "mark_valid_failed";
    case RollbackConfirmFailure::VERIFY_STATE_FAILED:
      return "verify_state_failed";
  }
  return "unknown";
}

void OtaRollbackConfirmBackend::fail_(RollbackConfirmFailure failure,
                                      esp_err_t error) {
  this->snapshot_.state = RollbackConfirmState::FAILED;
  this->snapshot_.failure = failure;
  this->snapshot_.last_error = error;
  this->snapshot_.confirmed = false;
}

bool OtaRollbackConfirmBackend::confirm() {
  this->snapshot_ = {};
  this->snapshot_.runtime_enabled = this->runtime_enabled_;
  if (!this->runtime_enabled_) {
    this->snapshot_.state = RollbackConfirmState::DISABLED;
    this->snapshot_.failure = RollbackConfirmFailure::DISABLED;
    this->snapshot_.last_error = ESP_ERR_NOT_SUPPORTED;
    return false;
  }

  const esp_partition_t *running_raw = esp_ota_get_running_partition();
  const esp_partition_t *running =
      running_raw == nullptr ? nullptr : esp_partition_verify(running_raw);
  if (running == nullptr) {
    this->fail_(RollbackConfirmFailure::INVALID_RUNNING_PARTITION,
                ESP_ERR_INVALID_ARG);
    return false;
  }
  this->snapshot_.running_address = running->address;
  if (running->type != ESP_PARTITION_TYPE_APP) {
    this->fail_(RollbackConfirmFailure::RUNNING_NOT_APP,
                ESP_ERR_INVALID_ARG);
    return false;
  }

  const esp_partition_t *boot_raw = esp_ota_get_boot_partition();
  const esp_partition_t *boot =
      boot_raw == nullptr ? nullptr : esp_partition_verify(boot_raw);
  if (boot == nullptr || boot->address != running->address) {
    this->snapshot_.boot_address = boot == nullptr ? 0 : boot->address;
    this->fail_(RollbackConfirmFailure::BOOT_PARTITION_MISMATCH,
                ESP_ERR_INVALID_STATE);
    return false;
  }
  this->snapshot_.boot_address = boot->address;

  esp_ota_img_states_t current_state = ESP_OTA_IMG_UNDEFINED;
  esp_err_t error = esp_ota_get_state_partition(running, &current_state);
  if (error != ESP_OK) {
    this->fail_(RollbackConfirmFailure::STATE_QUERY_FAILED, error);
    return false;
  }
  this->snapshot_.state_before = current_state;
  this->snapshot_.state_after = current_state;

  if (current_state == ESP_OTA_IMG_VALID) {
    this->snapshot_.state = RollbackConfirmState::ALREADY_VALID;
    this->snapshot_.failure = RollbackConfirmFailure::NONE;
    this->snapshot_.last_error = ESP_OK;
    this->snapshot_.confirmed = true;
    return true;
  }
  if (current_state != ESP_OTA_IMG_PENDING_VERIFY) {
    this->fail_(RollbackConfirmFailure::STATE_NOT_PENDING_VERIFY,
                ESP_ERR_INVALID_STATE);
    return false;
  }

  this->snapshot_.state = RollbackConfirmState::CONFIRMING;
  this->snapshot_.mark_called = true;
  error = esp_ota_mark_app_valid_cancel_rollback();
  if (error != ESP_OK) {
    this->fail_(RollbackConfirmFailure::MARK_VALID_FAILED, error);
    return false;
  }

  esp_ota_img_states_t verified_state = ESP_OTA_IMG_UNDEFINED;
  error = esp_ota_get_state_partition(running, &verified_state);
  this->snapshot_.state_after = verified_state;
  if (error != ESP_OK || verified_state != ESP_OTA_IMG_VALID) {
    this->fail_(RollbackConfirmFailure::VERIFY_STATE_FAILED,
                error == ESP_OK ? ESP_ERR_INVALID_STATE : error);
    return false;
  }

  this->snapshot_.state = RollbackConfirmState::CONFIRMED;
  this->snapshot_.failure = RollbackConfirmFailure::NONE;
  this->snapshot_.last_error = ESP_OK;
  this->snapshot_.confirmed = true;
  return true;
}

}  // namespace esphome::usp_ota_guard
