#include "ota_activation_coordinator.h"

#include <cstring>

namespace esphome::usp_ota_guard {

const char *OtaActivationCoordinator::state_name(
    ActivationCoordinatorState state) {
  switch (state) {
    case ActivationCoordinatorState::IDLE: return "idle";
    case ActivationCoordinatorState::BLOCKED: return "blocked";
    case ActivationCoordinatorState::TICKET_REJECTED:
      return "ticket_rejected";
    case ActivationCoordinatorState::ACTIVATING: return "activating";
    case ActivationCoordinatorState::ACTIVATED: return "activated";
    case ActivationCoordinatorState::FAILED: return "failed";
  }
  return "unknown";
}

const char *OtaActivationCoordinator::failure_name(
    ActivationCoordinatorFailure failure) {
  switch (failure) {
    case ActivationCoordinatorFailure::NONE: return "none";
    case ActivationCoordinatorFailure::INVALID_ARGUMENT:
      return "invalid_argument";
    case ActivationCoordinatorFailure::POLICY_BLOCKED:
      return "policy_blocked";
    case ActivationCoordinatorFailure::TICKET_REJECTED:
      return "ticket_rejected";
    case ActivationCoordinatorFailure::BACKEND_FAILED:
      return "backend_failed";
  }
  return "unknown";
}

void OtaActivationCoordinator::reset_() {
  this->snapshot_ = {};
  this->snapshot_.policy_reason = ActivationBlockReason::INVALID_TARGET;
}

void OtaActivationCoordinator::fail_(
    ActivationCoordinatorState state,
    ActivationCoordinatorFailure failure) {
  this->snapshot_.state = state;
  this->snapshot_.failure = failure;
}

ActivationCoordinatorSnapshot OtaActivationCoordinator::execute(
    const ActivationCoordinatorInput &input,
    const OtaActivationPolicy *policy,
    OtaActivationTicket *ticket,
    OtaBootActivationBackend *backend) {
  this->reset_();
  if (policy == nullptr || ticket == nullptr || backend == nullptr ||
      input.target == nullptr) {
    this->fail_(ActivationCoordinatorState::BLOCKED,
                ActivationCoordinatorFailure::INVALID_ARGUMENT);
    return this->snapshot_;
  }

  const size_t version_length =
      strnlen(input.target_version.data(), input.target_version.size());
  if (version_length == 0 || version_length >= input.target_version.size()) {
    this->fail_(ActivationCoordinatorState::BLOCKED,
                ActivationCoordinatorFailure::INVALID_ARGUMENT);
    return this->snapshot_;
  }

  const ActivationDecision decision =
      policy->evaluate(input.target, input.evidence);
  this->snapshot_.policy_reason = decision.reason;
  if (decision.eligibility != ActivationEligibility::ELIGIBLE ||
      decision.reason != ActivationBlockReason::NONE ||
      decision.target == nullptr) {
    this->fail_(ActivationCoordinatorState::BLOCKED,
                ActivationCoordinatorFailure::POLICY_BLOCKED);
    return this->snapshot_;
  }
  this->snapshot_.target_address = decision.target->address;

  if (!ticket->consume(input.ticket_hex, input.session_binding.data(),
                       decision.target->address,
                       input.candidate_sha256.data(),
                       input.target_version.data(), input.now_ms)) {
    this->snapshot_.ticket_failure = ticket->snapshot().failure;
    this->fail_(ActivationCoordinatorState::TICKET_REJECTED,
                ActivationCoordinatorFailure::TICKET_REJECTED);
    return this->snapshot_;
  }

  this->snapshot_.ticket_consumed = true;
  this->snapshot_.state = ActivationCoordinatorState::ACTIVATING;
  this->snapshot_.backend_attempted = true;
  if (!backend->select(decision.target)) {
    const BootActivationSnapshot backend_snapshot = backend->snapshot();
    this->snapshot_.backend_failure = backend_snapshot.failure;
    this->snapshot_.boot_selected = backend_snapshot.boot_selected;
    this->fail_(ActivationCoordinatorState::FAILED,
                ActivationCoordinatorFailure::BACKEND_FAILED);
    return this->snapshot_;
  }

  const BootActivationSnapshot backend_snapshot = backend->snapshot();
  this->snapshot_.backend_failure = backend_snapshot.failure;
  this->snapshot_.boot_selected = backend_snapshot.boot_selected;
  if (backend_snapshot.state != BootActivationState::ACTIVATED ||
      backend_snapshot.failure != BootActivationFailure::NONE ||
      !backend_snapshot.boot_selected) {
    this->fail_(ActivationCoordinatorState::FAILED,
                ActivationCoordinatorFailure::BACKEND_FAILED);
    return this->snapshot_;
  }

  this->snapshot_.state = ActivationCoordinatorState::ACTIVATED;
  this->snapshot_.failure = ActivationCoordinatorFailure::NONE;
  return this->snapshot_;
}

}  // namespace esphome::usp_ota_guard
