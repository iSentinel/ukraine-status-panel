#pragma once

#include <cstdint>

#include <esp_err.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

namespace esphome::usp_ota_guard {

enum class RollbackHealthEligibility : uint8_t {
  BLOCKED = 0,
  ELIGIBLE_TO_CONFIRM,
  ALREADY_VALID,
};

enum class RollbackHealthReason : uint8_t {
  NONE = 0,
  INVALID_RUNNING_PARTITION,
  RUNNING_NOT_APP,
  BOOT_PARTITION_MISMATCH,
  STATE_QUERY_FAILED,
  STATE_NOT_PENDING_VERIFY,
  STARTUP_INCOMPLETE,
  CORE_SERVICES_NOT_READY,
  CONFIG_NOT_READY,
  DISPLAY_NOT_READY,
  FATAL_FAULT_PRESENT,
  STABLE_UPTIME_TOO_SHORT,
};

struct RollbackHealthEvidence {
  bool startup_complete{false};
  bool core_services_ready{false};
  bool config_ready{false};
  bool display_ready{false};
  bool fatal_fault_free{false};
  uint32_t stable_uptime_ms{0};
};

struct RollbackHealthDecision {
  RollbackHealthEligibility eligibility{RollbackHealthEligibility::BLOCKED};
  RollbackHealthReason reason{RollbackHealthReason::INVALID_RUNNING_PARTITION};
  const esp_partition_t *running{nullptr};
  const esp_partition_t *boot{nullptr};
  esp_ota_img_states_t ota_state{ESP_OTA_IMG_UNDEFINED};
  esp_err_t state_query_error{ESP_OK};
};

// Read-only first-boot rollback health policy.
//
// This stage only decides whether a PENDING_VERIFY image is locally healthy
// enough to be confirmed by a later component. It never marks an app valid or
// invalid, never changes boot selection, never writes/erases flash, and never
// reboots. Network availability is intentionally absent from the evidence:
// a healthy panel must be confirmable while offline.
class OtaRollbackHealthPolicy final {
 public:
  static constexpr uint32_t MIN_STABLE_UPTIME_MS = 30000;

  RollbackHealthDecision evaluate(const RollbackHealthEvidence &evidence) const;

  static const char *eligibility_name(RollbackHealthEligibility eligibility);
  static const char *reason_name(RollbackHealthReason reason);
};

}  // namespace esphome::usp_ota_guard
