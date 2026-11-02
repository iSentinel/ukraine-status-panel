#include "ota_rollback_health_policy.h"

namespace esphome::usp_ota_guard {

const char *OtaRollbackHealthPolicy::eligibility_name(
    RollbackHealthEligibility eligibility) {
  switch (eligibility) {
    case RollbackHealthEligibility::BLOCKED: return "blocked";
    case RollbackHealthEligibility::ELIGIBLE_TO_CONFIRM: return "eligible_to_confirm";
    case RollbackHealthEligibility::ALREADY_VALID: return "already_valid";
  }
  return "unknown";
}

const char *OtaRollbackHealthPolicy::reason_name(RollbackHealthReason reason) {
  switch (reason) {
    case RollbackHealthReason::NONE: return "none";
    case RollbackHealthReason::INVALID_RUNNING_PARTITION:
      return "invalid_running_partition";
    case RollbackHealthReason::RUNNING_NOT_APP: return "running_not_app";
    case RollbackHealthReason::BOOT_PARTITION_MISMATCH:
      return "boot_partition_mismatch";
    case RollbackHealthReason::STATE_QUERY_FAILED: return "state_query_failed";
    case RollbackHealthReason::STATE_NOT_PENDING_VERIFY:
      return "state_not_pending_verify";
    case RollbackHealthReason::STARTUP_INCOMPLETE: return "startup_incomplete";
    case RollbackHealthReason::CORE_SERVICES_NOT_READY:
      return "core_services_not_ready";
    case RollbackHealthReason::CONFIG_NOT_READY: return "config_not_ready";
    case RollbackHealthReason::DISPLAY_NOT_READY: return "display_not_ready";
    case RollbackHealthReason::FATAL_FAULT_PRESENT:
      return "fatal_fault_present";
    case RollbackHealthReason::STABLE_UPTIME_TOO_SHORT:
      return "stable_uptime_too_short";
  }
  return "unknown";
}

RollbackHealthDecision OtaRollbackHealthPolicy::evaluate(
    const RollbackHealthEvidence &evidence) const {
  RollbackHealthDecision decision{};

  const esp_partition_t *running_raw = esp_ota_get_running_partition();
  decision.running =
      running_raw == nullptr ? nullptr : esp_partition_verify(running_raw);
  if (decision.running == nullptr) {
    decision.reason = RollbackHealthReason::INVALID_RUNNING_PARTITION;
    return decision;
  }
  if (decision.running->type != ESP_PARTITION_TYPE_APP) {
    decision.reason = RollbackHealthReason::RUNNING_NOT_APP;
    return decision;
  }

  const esp_partition_t *boot_raw = esp_ota_get_boot_partition();
  decision.boot = boot_raw == nullptr ? nullptr : esp_partition_verify(boot_raw);
  if (decision.boot == nullptr ||
      decision.boot->address != decision.running->address) {
    decision.reason = RollbackHealthReason::BOOT_PARTITION_MISMATCH;
    return decision;
  }

  decision.state_query_error =
      esp_ota_get_state_partition(decision.running, &decision.ota_state);
  if (decision.state_query_error != ESP_OK) {
    decision.reason = RollbackHealthReason::STATE_QUERY_FAILED;
    return decision;
  }

  if (decision.ota_state == ESP_OTA_IMG_VALID) {
    decision.eligibility = RollbackHealthEligibility::ALREADY_VALID;
    decision.reason = RollbackHealthReason::NONE;
    return decision;
  }
  if (decision.ota_state != ESP_OTA_IMG_PENDING_VERIFY) {
    decision.reason = RollbackHealthReason::STATE_NOT_PENDING_VERIFY;
    return decision;
  }

  if (!evidence.startup_complete) {
    decision.reason = RollbackHealthReason::STARTUP_INCOMPLETE;
    return decision;
  }
  if (!evidence.core_services_ready) {
    decision.reason = RollbackHealthReason::CORE_SERVICES_NOT_READY;
    return decision;
  }
  if (!evidence.config_ready) {
    decision.reason = RollbackHealthReason::CONFIG_NOT_READY;
    return decision;
  }
  if (!evidence.display_ready) {
    decision.reason = RollbackHealthReason::DISPLAY_NOT_READY;
    return decision;
  }
  if (!evidence.fatal_fault_free) {
    decision.reason = RollbackHealthReason::FATAL_FAULT_PRESENT;
    return decision;
  }
  if (evidence.stable_uptime_ms < MIN_STABLE_UPTIME_MS) {
    decision.reason = RollbackHealthReason::STABLE_UPTIME_TOO_SHORT;
    return decision;
  }

  decision.eligibility = RollbackHealthEligibility::ELIGIBLE_TO_CONFIRM;
  decision.reason = RollbackHealthReason::NONE;
  return decision;
}

}  // namespace esphome::usp_ota_guard
