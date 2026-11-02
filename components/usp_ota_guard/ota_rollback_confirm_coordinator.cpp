#include "ota_rollback_confirm_coordinator.h"

namespace esphome::usp_ota_guard {

const char *OtaRollbackConfirmCoordinator::state_name(
    RollbackConfirmCoordinatorState state) {
  switch (state) {
    case RollbackConfirmCoordinatorState::IDLE: return "idle";
    case RollbackConfirmCoordinatorState::BLOCKED: return "blocked";
    case RollbackConfirmCoordinatorState::CONFIRMING: return "confirming";
    case RollbackConfirmCoordinatorState::CONFIRMED: return "confirmed";
    case RollbackConfirmCoordinatorState::ALREADY_VALID: return "already_valid";
    case RollbackConfirmCoordinatorState::FAILED: return "failed";
  }
  return "unknown";
}

const char *OtaRollbackConfirmCoordinator::failure_name(
    RollbackConfirmCoordinatorFailure failure) {
  switch (failure) {
    case RollbackConfirmCoordinatorFailure::NONE: return "none";
    case RollbackConfirmCoordinatorFailure::INVALID_ARGUMENT:
      return "invalid_argument";
    case RollbackConfirmCoordinatorFailure::POLICY_BLOCKED:
      return "policy_blocked";
    case RollbackConfirmCoordinatorFailure::BACKEND_FAILED:
      return "backend_failed";
  }
  return "unknown";
}

void OtaRollbackConfirmCoordinator::reset_() {
  this->snapshot_ = {};
  this->snapshot_.health_reason = RollbackHealthReason::INVALID_RUNNING_PARTITION;
}

void OtaRollbackConfirmCoordinator::fail_(
    RollbackConfirmCoordinatorFailure failure) {
  this->snapshot_.state = RollbackConfirmCoordinatorState::FAILED;
  this->snapshot_.failure = failure;
}

bool OtaRollbackConfirmCoordinator::accept_backend_snapshot_(
    const RollbackConfirmSnapshot &backend_snapshot) {
  this->snapshot_.backend_state = backend_snapshot.state;
  this->snapshot_.backend_failure = backend_snapshot.failure;
  if (backend_snapshot.failure != RollbackConfirmFailure::NONE ||
      !backend_snapshot.confirmed) {
    return false;
  }
  if (backend_snapshot.state == RollbackConfirmState::CONFIRMED) {
    this->snapshot_.state = RollbackConfirmCoordinatorState::CONFIRMED;
    this->snapshot_.confirmed = true;
    return true;
  }
  if (backend_snapshot.state == RollbackConfirmState::ALREADY_VALID) {
    this->snapshot_.state = RollbackConfirmCoordinatorState::ALREADY_VALID;
    this->snapshot_.confirmed = true;
    this->snapshot_.already_valid = true;
    return true;
  }
  return false;
}

RollbackConfirmCoordinatorSnapshot OtaRollbackConfirmCoordinator::execute(
    const RollbackHealthEvidence &evidence,
    const OtaRollbackHealthPolicy *policy,
    OtaRollbackConfirmBackend *backend) {
  this->reset_();
  if (policy == nullptr || backend == nullptr) {
    this->snapshot_.state = RollbackConfirmCoordinatorState::BLOCKED;
    this->snapshot_.failure = RollbackConfirmCoordinatorFailure::INVALID_ARGUMENT;
    return this->snapshot_;
  }

  const RollbackHealthDecision decision = policy->evaluate(evidence);
  this->snapshot_.health_eligibility = decision.eligibility;
  this->snapshot_.health_reason = decision.reason;
  if (decision.eligibility == RollbackHealthEligibility::ALREADY_VALID &&
      decision.reason == RollbackHealthReason::NONE) {
    this->snapshot_.state = RollbackConfirmCoordinatorState::ALREADY_VALID;
    this->snapshot_.confirmed = true;
    this->snapshot_.already_valid = true;
    return this->snapshot_;
  }
  if (decision.eligibility != RollbackHealthEligibility::ELIGIBLE_TO_CONFIRM ||
      decision.reason != RollbackHealthReason::NONE) {
    this->snapshot_.state = RollbackConfirmCoordinatorState::BLOCKED;
    this->snapshot_.failure = RollbackConfirmCoordinatorFailure::POLICY_BLOCKED;
    return this->snapshot_;
  }

  this->snapshot_.state = RollbackConfirmCoordinatorState::CONFIRMING;
  this->snapshot_.backend_attempted = true;
  const bool backend_ok = backend->confirm();
  const RollbackConfirmSnapshot backend_snapshot = backend->snapshot();
  if (!backend_ok || !this->accept_backend_snapshot_(backend_snapshot)) {
    this->fail_(RollbackConfirmCoordinatorFailure::BACKEND_FAILED);
    return this->snapshot_;
  }
  this->snapshot_.failure = RollbackConfirmCoordinatorFailure::NONE;
  return this->snapshot_;
}

}  // namespace esphome::usp_ota_guard
