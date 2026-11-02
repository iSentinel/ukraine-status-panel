#pragma once

#include <cstdint>

#include <esp_err.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

namespace esphome::usp_ota_guard {

enum class RollbackConfirmState : uint8_t {
  DISABLED = 0,
  IDLE,
  CONFIRMING,
  CONFIRMED,
  ALREADY_VALID,
  FAILED,
};

enum class RollbackConfirmFailure : uint8_t {
  NONE = 0,
  DISABLED,
  INVALID_RUNNING_PARTITION,
  RUNNING_NOT_APP,
  BOOT_PARTITION_MISMATCH,
  STATE_QUERY_FAILED,
  STATE_NOT_PENDING_VERIFY,
  MARK_VALID_FAILED,
  VERIFY_STATE_FAILED,
};

struct RollbackConfirmSnapshot {
  RollbackConfirmState state{RollbackConfirmState::DISABLED};
  RollbackConfirmFailure failure{RollbackConfirmFailure::NONE};
  esp_err_t last_error{ESP_OK};
  esp_ota_img_states_t state_before{ESP_OTA_IMG_UNDEFINED};
  esp_ota_img_states_t state_after{ESP_OTA_IMG_UNDEFINED};
  uint32_t running_address{0};
  uint32_t boot_address{0};
  bool runtime_enabled{false};
  bool mark_called{false};
  bool confirmed{false};
};

// Compile-only ESP-IDF rollback-confirm backend foundation.
//
// 0.115.175 deliberately keeps this class hard-disabled and unwired. There is
// no runtime-enable setter, global instance, HTTP route, reboot path, invalid
// rollback path, boot-selection path, writer, or erase primitive. The real
// ESP-IDF mark-valid API is compiled only to validate exact failure ordering.
class OtaRollbackConfirmBackend final {
 public:
  bool confirm();

  RollbackConfirmSnapshot snapshot() const { return this->snapshot_; }
  static const char *state_name(RollbackConfirmState state);
  static const char *failure_name(RollbackConfirmFailure failure);

 private:
  void fail_(RollbackConfirmFailure failure, esp_err_t error);

  // Hard-disabled in this checkpoint. Deliberately no setter exists.
  bool runtime_enabled_{false};
  RollbackConfirmSnapshot snapshot_{};
};

}  // namespace esphome::usp_ota_guard
