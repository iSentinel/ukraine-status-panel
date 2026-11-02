#pragma once

#include <cstdint>

#include <esp_partition.h>

#include "ota_post_write_validator.h"

namespace esphome::usp_ota_guard {

enum class ActivationEligibility : uint8_t {
  INELIGIBLE = 0,
  ELIGIBLE,
};

enum class ActivationBlockReason : uint8_t {
  NONE = 0,
  INVALID_TARGET,
  NOT_APP_PARTITION,
  TARGET_IS_RUNNING,
  MANIFEST_NOT_VERIFIED,
  IMAGE_NOT_VERIFIED,
  WRITE_NOT_FINALIZED,
  POST_WRITE_NOT_VALID,
  SESSION_NOT_CURRENT,
};

struct ActivationEvidence {
  bool manifest_verified{false};
  bool image_verified{false};
  bool write_finalized{false};
  bool session_current{false};
  PostWriteValidationSnapshot post_write{};
};

struct ActivationDecision {
  ActivationEligibility eligibility{ActivationEligibility::INELIGIBLE};
  ActivationBlockReason reason{ActivationBlockReason::INVALID_TARGET};
  const esp_partition_t *target{nullptr};
  const esp_partition_t *running{nullptr};
};

// Compile-only policy for deciding whether a finalized OTA candidate is even
// eligible for a future, separately-reviewed activation step.
//
// This class is intentionally read-only and unwired. It never selects a boot
// partition, writes/erases flash, restarts the device, or creates an activation
// capability. A future activation implementation must still perform its own
// authenticated one-time authorization and revalidate this decision.
class OtaActivationPolicy final {
 public:
  ActivationDecision evaluate(const esp_partition_t *target,
                              const ActivationEvidence &evidence) const;

  static const char *eligibility_name(ActivationEligibility eligibility);
  static const char *reason_name(ActivationBlockReason reason);
};

}  // namespace esphome::usp_ota_guard
