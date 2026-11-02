#include "ota_activation_policy.h"

#include <esp_ota_ops.h>

namespace esphome::usp_ota_guard {

const char *OtaActivationPolicy::eligibility_name(
    ActivationEligibility eligibility) {
  switch (eligibility) {
    case ActivationEligibility::INELIGIBLE: return "ineligible";
    case ActivationEligibility::ELIGIBLE: return "eligible";
  }
  return "unknown";
}

const char *OtaActivationPolicy::reason_name(ActivationBlockReason reason) {
  switch (reason) {
    case ActivationBlockReason::NONE: return "none";
    case ActivationBlockReason::INVALID_TARGET: return "invalid_target";
    case ActivationBlockReason::NOT_APP_PARTITION: return "not_app_partition";
    case ActivationBlockReason::TARGET_IS_RUNNING: return "target_is_running";
    case ActivationBlockReason::MANIFEST_NOT_VERIFIED: return "manifest_not_verified";
    case ActivationBlockReason::IMAGE_NOT_VERIFIED: return "image_not_verified";
    case ActivationBlockReason::WRITE_NOT_FINALIZED: return "write_not_finalized";
    case ActivationBlockReason::POST_WRITE_NOT_VALID: return "post_write_not_valid";
    case ActivationBlockReason::SESSION_NOT_CURRENT: return "session_not_current";
  }
  return "unknown";
}

ActivationDecision OtaActivationPolicy::evaluate(
    const esp_partition_t *target, const ActivationEvidence &evidence) const {
  ActivationDecision decision{};
  decision.target = target;
  decision.running = esp_ota_get_running_partition();

  const esp_partition_t *verified_target =
      target == nullptr ? nullptr : esp_partition_verify(target);
  if (verified_target == nullptr || decision.running == nullptr) {
    decision.reason = ActivationBlockReason::INVALID_TARGET;
    return decision;
  }
  decision.target = verified_target;
  if (verified_target->type != ESP_PARTITION_TYPE_APP) {
    decision.reason = ActivationBlockReason::NOT_APP_PARTITION;
    return decision;
  }
  if (verified_target->address == decision.running->address) {
    decision.reason = ActivationBlockReason::TARGET_IS_RUNNING;
    return decision;
  }
  if (!evidence.manifest_verified) {
    decision.reason = ActivationBlockReason::MANIFEST_NOT_VERIFIED;
    return decision;
  }
  if (!evidence.image_verified) {
    decision.reason = ActivationBlockReason::IMAGE_NOT_VERIFIED;
    return decision;
  }
  if (!evidence.write_finalized) {
    decision.reason = ActivationBlockReason::WRITE_NOT_FINALIZED;
    return decision;
  }
  if (evidence.post_write.state != PostWriteValidationState::VALID ||
      !evidence.post_write.descriptor_ok || !evidence.post_write.digest_match ||
      evidence.post_write.failure != PostWriteValidationFailure::NONE) {
    decision.reason = ActivationBlockReason::POST_WRITE_NOT_VALID;
    return decision;
  }
  if (!evidence.session_current) {
    decision.reason = ActivationBlockReason::SESSION_NOT_CURRENT;
    return decision;
  }

  decision.eligibility = ActivationEligibility::ELIGIBLE;
  decision.reason = ActivationBlockReason::NONE;
  return decision;
}

}  // namespace esphome::usp_ota_guard
