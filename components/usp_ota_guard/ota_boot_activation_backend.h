#pragma once

#include <cstdint>

#include <esp_err.h>
#include <esp_partition.h>

namespace esphome::usp_ota_guard {

enum class BootActivationState : uint8_t {
  DISABLED = 0,
  IDLE,
  ACTIVATING,
  ACTIVATED,
  FAILED,
};

enum class BootActivationFailure : uint8_t {
  NONE = 0,
  DISABLED,
  INVALID_TARGET,
  NOT_APP_PARTITION,
  TARGET_IS_RUNNING,
  SET_BOOT_FAILED,
  VERIFY_BOOT_FAILED,
};

struct BootActivationSnapshot {
  BootActivationState state{BootActivationState::DISABLED};
  BootActivationFailure failure{BootActivationFailure::NONE};
  esp_err_t last_error{ESP_OK};
  uint32_t target_address{0};
  uint32_t running_address{0};
  uint32_t boot_address{0};
  bool runtime_enabled{false};
  bool boot_selected{false};
};

// Compile-only ESP-IDF boot-selection backend foundation.
//
// 0.115.172 deliberately keeps this class hard-disabled and unwired. There is
// no runtime-enable setter, global instance, HTTP route, reboot path, rollback
// marking, flash writer, or erase primitive. The real boot-selection API is
// compiled only so its exact ESP-IDF 5.5.5 contract can be validated before a
// later, separately reviewed coordinator is considered.
class OtaBootActivationBackend final {
 public:
  bool select(const esp_partition_t *target);

  BootActivationSnapshot snapshot() const { return this->snapshot_; }
  static const char *state_name(BootActivationState state);
  static const char *failure_name(BootActivationFailure failure);

 private:
  void fail_(BootActivationFailure failure, esp_err_t error);

  // Hard-disabled in this checkpoint. Deliberately no setter exists.
  bool runtime_enabled_{false};
  BootActivationSnapshot snapshot_{};
};

}  // namespace esphome::usp_ota_guard
