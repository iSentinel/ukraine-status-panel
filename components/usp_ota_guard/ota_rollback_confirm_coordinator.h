#pragma once

#include <cstdint>

#include "ota_rollback_confirm_backend.h"
#include "ota_rollback_health_policy.h"

namespace esphome::usp_ota_guard {

enum class RollbackConfirmCoordinatorState : uint8_t {
  IDLE = 0,
  BLOCKED,
  CONFIRMING,
  CONFIRMED,
  ALREADY_VALID,
  FAILED,
};

enum class RollbackConfirmCoordinatorFailure : uint8_t {
  NONE = 0,
  INVALID_ARGUMENT,
  POLICY_BLOCKED,
  BACKEND_FAILED,
};

struct RollbackConfirmCoordinatorSnapshot {
  RollbackConfirmCoordinatorState state{RollbackConfirmCoordinatorState::IDLE};
  RollbackConfirmCoordinatorFailure failure{RollbackConfirmCoordinatorFailure::NONE};
  RollbackHealthEligibility health_eligibility{RollbackHealthEligibility::BLOCKED};
  RollbackHealthReason health_reason{RollbackHealthReason::INVALID_RUNNING_PARTITION};
  RollbackConfirmState backend_state{RollbackConfirmState::DISABLED};
  RollbackConfirmFailure backend_failure{RollbackConfirmFailure::NONE};
  bool backend_attempted{false};
  bool confirmed{false};
  bool already_valid{false};
};

// Compile-only bridge between rollback health policy and confirm backend.
//
// This class stays completely unwired in 0.115.176. It owns no mutation
// primitive: the read-only policy is evaluated first, ALREADY_VALID returns
// without touching the backend, and only ELIGIBLE_TO_CONFIRM may reach the
// still hard-disabled backend. There is no global instance, HTTP route,
// runtime enable path, reboot, writer/erase path, or boot-selection path.
class OtaRollbackConfirmCoordinator final {
 public:
  RollbackConfirmCoordinatorSnapshot execute(
      const RollbackHealthEvidence &evidence,
      const OtaRollbackHealthPolicy *policy,
      OtaRollbackConfirmBackend *backend);

  RollbackConfirmCoordinatorSnapshot snapshot() const { return this->snapshot_; }

  static const char *state_name(RollbackConfirmCoordinatorState state);
  static const char *failure_name(RollbackConfirmCoordinatorFailure failure);

 private:
  void reset_();
  void fail_(RollbackConfirmCoordinatorFailure failure);
  bool accept_backend_snapshot_(const RollbackConfirmSnapshot &backend_snapshot);

  RollbackConfirmCoordinatorSnapshot snapshot_{};
};

}  // namespace esphome::usp_ota_guard
