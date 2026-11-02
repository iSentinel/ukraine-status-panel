#include "optional_sht4x.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cmath>
#include <cinttypes>

namespace esphome::usp_optional_sht4x {

static const char *const TAG = "usp.optional_sht4x";
static constexpr uint8_t MEASURE_HIGH_PRECISION = 0xFD;
static constexpr uint8_t SERIAL_NUMBER_COMMAND = 0x89;
static constexpr float RAW_MAX = 65535.0f;

void OptionalSHT4XComponent::setup() {
  if (this->probe_()) {
    this->reprobe_policy_.clear();
    return;
  }
  this->reprobe_policy_.schedule(millis(), this->ever_detected_);
  ESP_LOGI(TAG, "Optional SHT4x not detected at 0x%02X", this->address_);
}

void OptionalSHT4XComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Optional SHT4x:");
  LOG_I2C_DEVICE(this);
  ESP_LOGCONFIG(TAG, "  Detected: %s", YESNO(this->detected_));
  if (this->detected_) {
    ESP_LOGCONFIG(TAG, "  Serial number: %08" PRIx32, this->serial_number_);
  }
}

bool OptionalSHT4XComponent::probe_() {
  const uint8_t command = SERIAL_NUMBER_COMMAND;
  const auto write_error = this->write(&command, 1);
  if (write_error == i2c::ERROR_NOT_ACKNOWLEDGED) {
    if (!this->ever_detected_) {
      this->status_clear_warning();
    }
    return false;
  }
  if (write_error != i2c::ERROR_OK) {
    this->status_set_warning("SHT4x probe I2C fault");
    ESP_LOGW(TAG, "Optional SHT4x probe write failed err=%d", write_error);
    return false;
  }

  delay(1);
  uint8_t raw[6]{};
  const auto read_error = this->read(raw, sizeof(raw));
  if (read_error != i2c::ERROR_OK) {
    this->status_set_warning("SHT4x probe I2C fault");
    ESP_LOGW(TAG, "Optional SHT4x probe read failed err=%d", read_error);
    return false;
  }

  for (size_t offset = 0; offset < sizeof(raw); offset += 3) {
    const uint8_t expected = crc8(
        &raw[offset], 2, 0xFF, sensirion_common::CRC_POLYNOMIAL, true);
    if (expected != raw[offset + 2]) {
      this->status_set_warning("SHT4x probe CRC fault");
      ESP_LOGW(TAG, "Optional SHT4x probe CRC mismatch word=%u",
               static_cast<unsigned>(offset / 3));
      return false;
    }
  }

  const uint16_t serial_hi = (uint16_t(raw[0]) << 8) | raw[1];
  const uint16_t serial_lo = (uint16_t(raw[3]) << 8) | raw[4];
  const bool newly_detected = !this->detected_;
  this->detected_ = true;
  this->ever_detected_ = true;
  this->last_read_ok_ = false;
  this->io_failures_ = 0;
  this->serial_number_ = (uint32_t(serial_hi) << 16) | serial_lo;
  this->status_clear_warning();
  if (newly_detected) {
    ESP_LOGI(TAG, "Optional SHT4x detected at 0x%02X", this->address_);
  }
  return true;
}

bool OptionalSHT4XComponent::probe_if_due_(uint32_t now) {
  if (!this->reprobe_policy_.due(now)) {
    return false;
  }
  if (!this->probe_()) {
    this->reprobe_policy_.schedule(now, this->ever_detected_);
    return false;
  }
  this->reprobe_policy_.clear();
  return true;
}

void OptionalSHT4XComponent::update() {
  if (!this->detected_ && !this->probe_if_due_(millis())) {
    return;
  }
  if (!this->write_command(MEASURE_HIGH_PRECISION)) {
    this->note_io_failure_();
    return;
  }
  this->set_timeout(10, [this]() { this->read_measurement_(); });
}

void OptionalSHT4XComponent::read_measurement_() {
  uint16_t data[2]{};
  if (!this->read_data(data, 2)) {
    this->note_io_failure_();
    return;
  }

  this->io_failures_ = 0;
  this->last_read_ok_ = true;
  this->status_clear_warning();
  const float temperature = -45.0f + 175.0f * float(data[0]) / RAW_MAX;
  const float humidity_raw = -6.0f + 125.0f * float(data[1]) / RAW_MAX;
  const float humidity = std::clamp(humidity_raw, 0.0f, 100.0f);
  if (this->temperature_sensor_ != nullptr) {
    this->temperature_sensor_->publish_state(temperature);
  }
  if (this->humidity_sensor_ != nullptr) {
    this->humidity_sensor_->publish_state(humidity);
  }
}

void OptionalSHT4XComponent::note_io_failure_() {
  this->last_read_ok_ = false;
  this->io_failures_++;
  if (this->io_failures_ < 3) {
    this->status_set_warning("SHT4x communication retry");
    return;
  }

  ESP_LOGW(TAG, "Optional SHT4x disconnected; waiting for it to return");
  this->detected_ = false;
  this->last_read_ok_ = false;
  this->serial_number_ = 0;
  this->io_failures_ = 0;
  this->reprobe_policy_.schedule(millis(), true);
  this->status_set_warning("SHT4x disconnected");
  this->publish_unavailable_();
}

void OptionalSHT4XComponent::publish_unavailable_() {
  if (this->temperature_sensor_ != nullptr) {
    this->temperature_sensor_->publish_state(NAN);
  }
  if (this->humidity_sensor_ != nullptr) {
    this->humidity_sensor_->publish_state(NAN);
  }
}

}  // namespace esphome::usp_optional_sht4x
