#pragma once

#include "reprobe_policy.h"

#include "esphome/components/sensirion_common/i2c_sensirion.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"

#include <cstdint>

namespace esphome::usp_optional_sht4x {

class OptionalSHT4XComponent final : public PollingComponent,
                                     public sensirion_common::SensirionI2CDevice {
 public:
  void setup() override;
  void dump_config() override;
  void update() override;

  void set_temperature_sensor(sensor::Sensor *sensor) { this->temperature_sensor_ = sensor; }
  void set_humidity_sensor(sensor::Sensor *sensor) { this->humidity_sensor_ = sensor; }

  bool detected() const { return this->detected_; }
  bool last_read_ok() const { return this->last_read_ok_; }
  uint32_t serial_number() const { return this->serial_number_; }

 protected:
  bool probe_();
  bool probe_if_due_(uint32_t now);
  void read_measurement_();
  void note_io_failure_();
  void publish_unavailable_();

  sensor::Sensor *temperature_sensor_{nullptr};
  sensor::Sensor *humidity_sensor_{nullptr};
  bool detected_{false};
  bool ever_detected_{false};
  bool last_read_ok_{false};
  uint8_t io_failures_{0};
  uint32_t serial_number_{0};
  ReprobePolicy reprobe_policy_{};
};

}  // namespace esphome::usp_optional_sht4x
