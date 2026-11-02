#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "ota_activation_policy.h"
#include "ota_activation_ticket.h"
#include "ota_boot_activation_backend.h"

namespace esphome::usp_ota_guard {

enum class ActivationCoordinatorState : uint8_t {
  IDLE = 0,
  BLOCKED,
  TICKET_REJECTED,
  ACTIVATING,
  ACTIVATED,
  FAILED,
};

enum class ActivationCoordinatorFailure : uint8_t {
  NONE = 0,
  INVALID_ARGUMENT,
  POLICY_BLOCKED,
  TICKET_REJECTED,
  BACKEND_FAILED,
};

struct ActivationCoordinatorInput {
  const esp_partition_t *target{nullptr};
  ActivationEvidence evidence{};
  std::string ticket_hex{};
  std::array<uint8_t, OtaActivationTicket::SESSION_BINDING_BYTES>
      session_binding{};
  std::array<uint8_t, OtaActivationTicket::SHA256_BYTES>
      candidate_sha256{};
  std::array<char, OtaActivationTicket::VERSION_CAPACITY> target_version{};
  uint32_t now_ms{0};
};

struct ActivationCoordinatorSnapshot {
  ActivationCoordinatorState state{ActivationCoordinatorState::IDLE};
  ActivationCoordinatorFailure failure{ActivationCoordinatorFailure::NONE};
  ActivationBlockReason policy_reason{ActivationBlockReason::INVALID_TARGET};
  ActivationTicketFailure ticket_failure{ActivationTicketFailure::NONE};
  BootActivationFailure backend_failure{BootActivationFailure::NONE};
  uint32_t target_address{0};
  bool ticket_consumed{false};
  bool backend_attempted{false};
  bool boot_selected{false};
};

// Compile-only coordinator for the future private activation path.
//
// It re-evaluates the read-only activation policy immediately before consuming
// the one-time activation ticket. Policy or ticket rejection therefore occurs
// before the boot backend can be called. After a successful consume the ticket
// is irreversibly destroyed; a backend failure requires a fresh ticket.
//
// This class is deliberately unwired. It exposes no HTTP route, global instance,
// reboot path, direct ESP-IDF mutation primitive, writer, erase API, or runtime
// enable mechanism. In 0.115.173 the backend it calls remains hard-disabled.
class OtaActivationCoordinator final {
 public:
  ActivationCoordinatorSnapshot execute(
      const ActivationCoordinatorInput &input,
      const OtaActivationPolicy *policy,
      OtaActivationTicket *ticket,
      OtaBootActivationBackend *backend);

  ActivationCoordinatorSnapshot snapshot() const { return this->snapshot_; }

  static const char *state_name(ActivationCoordinatorState state);
  static const char *failure_name(ActivationCoordinatorFailure failure);

 private:
  void reset_();
  void fail_(ActivationCoordinatorState state,
             ActivationCoordinatorFailure failure);

  ActivationCoordinatorSnapshot snapshot_{};
};

}  // namespace esphome::usp_ota_guard
