#include "ota_flash_backend.h"

namespace esphome::usp_ota_guard {

OtaFlashBackend::~OtaFlashBackend() {
  if (this->handle_active_) {
    esp_ota_abort(this->handle_);
    this->handle_active_ = false;
  }
}

const char *OtaFlashBackend::state_name(FlashBackendState state) {
  switch (state) {
    case FlashBackendState::DISABLED: return "disabled";
    case FlashBackendState::IDLE: return "idle";
    case FlashBackendState::OPEN: return "open";
    case FlashBackendState::WRITING: return "writing";
    case FlashBackendState::FINALIZED: return "finalized";
    case FlashBackendState::ABORTED: return "aborted";
    case FlashBackendState::FAILED: return "failed";
  }
  return "unknown";
}

void OtaFlashBackend::reset_transaction_() {
  if (this->handle_active_) {
    esp_ota_abort(this->handle_);
    this->handle_active_ = false;
  }
  this->partition_ = nullptr;
  this->handle_ = 0;
  this->last_error_ = ESP_OK;
  this->expected_bytes_ = 0;
  this->written_bytes_ = 0;
  this->state_ = this->runtime_enabled_ ? FlashBackendState::IDLE
                                        : FlashBackendState::DISABLED;
}

void OtaFlashBackend::fail_(esp_err_t error) {
  if (this->handle_active_) {
    esp_ota_abort(this->handle_);
    this->handle_active_ = false;
  }
  this->handle_ = 0;
  this->last_error_ = error;
  this->state_ = FlashBackendState::FAILED;
}

bool OtaFlashBackend::begin(size_t image_size) {
  if (!this->runtime_enabled_) {
    this->state_ = FlashBackendState::DISABLED;
    this->last_error_ = ESP_ERR_NOT_SUPPORTED;
    return false;
  }
  if (this->handle_active_) {
    this->fail_(ESP_ERR_INVALID_STATE);
    return false;
  }
  this->reset_transaction_();
  if (image_size == 0) {
    this->fail_(ESP_ERR_INVALID_ARG);
    return false;
  }

  this->partition_ = esp_ota_get_next_update_partition(nullptr);
  if (this->partition_ == nullptr) {
    this->fail_(ESP_ERR_NOT_FOUND);
    return false;
  }
  if (image_size > this->partition_->size) {
    this->fail_(ESP_ERR_INVALID_SIZE);
    return false;
  }

  const esp_err_t error = esp_ota_begin(this->partition_, image_size,
                                        &this->handle_);
  if (error != ESP_OK) {
    this->fail_(error);
    return false;
  }
  this->handle_active_ = true;
  this->expected_bytes_ = image_size;
  this->written_bytes_ = 0;
  this->state_ = FlashBackendState::OPEN;
  return true;
}

bool OtaFlashBackend::write(const uint8_t *data, size_t length) {
  if (!this->runtime_enabled_) {
    this->state_ = FlashBackendState::DISABLED;
    this->last_error_ = ESP_ERR_NOT_SUPPORTED;
    return false;
  }
  if (!this->handle_active_ || data == nullptr || length == 0) {
    this->fail_(ESP_ERR_INVALID_STATE);
    return false;
  }
  if (length > this->expected_bytes_ - this->written_bytes_) {
    this->fail_(ESP_ERR_INVALID_SIZE);
    return false;
  }

  const esp_err_t error = esp_ota_write(this->handle_, data, length);
  if (error != ESP_OK) {
    this->fail_(error);
    return false;
  }
  this->written_bytes_ += length;
  this->state_ = FlashBackendState::WRITING;
  return true;
}

bool OtaFlashBackend::finish() {
  if (!this->runtime_enabled_) {
    this->state_ = FlashBackendState::DISABLED;
    this->last_error_ = ESP_ERR_NOT_SUPPORTED;
    return false;
  }
  if (!this->handle_active_ ||
      this->written_bytes_ != this->expected_bytes_) {
    this->fail_(ESP_ERR_INVALID_SIZE);
    return false;
  }

  const esp_ota_handle_t handle = this->handle_;
  this->handle_active_ = false;
  this->handle_ = 0;
  const esp_err_t error = esp_ota_end(handle);
  if (error != ESP_OK) {
    this->last_error_ = error;
    this->state_ = FlashBackendState::FAILED;
    return false;
  }
  this->last_error_ = ESP_OK;
  this->state_ = FlashBackendState::FINALIZED;
  return true;
}

void OtaFlashBackend::abort() {
  esp_err_t error = ESP_OK;
  if (this->handle_active_) {
    error = esp_ota_abort(this->handle_);
  }
  this->handle_active_ = false;
  this->handle_ = 0;
  this->partition_ = nullptr;
  this->expected_bytes_ = 0;
  this->written_bytes_ = 0;
  this->last_error_ = error;
  this->state_ = error == ESP_OK ? FlashBackendState::ABORTED
                                 : FlashBackendState::FAILED;
}

FlashBackendSnapshot OtaFlashBackend::snapshot() const {
  FlashBackendSnapshot snapshot;
  snapshot.state = this->state_;
  snapshot.last_error = this->last_error_;
  snapshot.expected_bytes = this->expected_bytes_;
  snapshot.written_bytes = this->written_bytes_;
  snapshot.handle_active = this->handle_active_;
  snapshot.runtime_enabled = this->runtime_enabled_;
  return snapshot;
}

}  // namespace esphome::usp_ota_guard
