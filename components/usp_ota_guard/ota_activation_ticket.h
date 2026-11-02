#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "ota_activation_policy.h"

namespace esphome::usp_ota_guard {

enum class ActivationTicketState : uint8_t {
  EMPTY = 0,
  ACTIVE,
  CONSUMED,
  EXPIRED,
};

enum class ActivationTicketFailure : uint8_t {
  NONE = 0,
  INELIGIBLE_DECISION,
  INVALID_ARGUMENT,
  NOT_ACTIVE,
  EXPIRED,
  TOKEN_MISMATCH,
  SESSION_MISMATCH,
  TARGET_MISMATCH,
  DIGEST_MISMATCH,
  VERSION_MISMATCH,
};

struct ActivationTicketSnapshot {
  ActivationTicketState state{ActivationTicketState::EMPTY};
  ActivationTicketFailure failure{ActivationTicketFailure::NONE};
  uint32_t started_ms{0};
  uint32_t target_address{0};
  uint32_t ttl_ms{0};
  bool active{false};
};

// Compile-only, unwired one-time capability for a future activation step.
//
// A ticket may only be issued from an ELIGIBLE activation decision. It is bound
// to a session identifier, target partition address, candidate SHA256 and target
// version. Wrong bindings never consume a valid ticket. A successful consume is
// one-time and irreversibly destroys the secret. This class does not select a
// boot partition, write/erase flash, restart the device, or expose HTTP routes.
class OtaActivationTicket final {
 public:
  static constexpr size_t TICKET_BYTES = 16;
  static constexpr size_t SESSION_BINDING_BYTES = 16;
  static constexpr size_t SHA256_BYTES = 32;
  static constexpr size_t VERSION_CAPACITY = 32;
  static constexpr uint32_t TICKET_LIFETIME_MS = 30U * 1000U;

  bool issue(const ActivationDecision &decision,
             const uint8_t session_binding[SESSION_BINDING_BYTES],
             const uint8_t candidate_sha256[SHA256_BYTES],
             const char *target_version, uint32_t now_ms);

  bool consume(const std::string &ticket_hex,
               const uint8_t session_binding[SESSION_BINDING_BYTES],
               uint32_t target_address,
               const uint8_t candidate_sha256[SHA256_BYTES],
               const char *target_version, uint32_t now_ms);

  void expire_if_needed(uint32_t now_ms);
  void clear();
  std::string ticket_hex() const;
  uint32_t remaining_ms(uint32_t now_ms) const;
  ActivationTicketSnapshot snapshot() const;

  static const char *state_name(ActivationTicketState state);
  static const char *failure_name(ActivationTicketFailure failure);

 private:
  bool expired_(uint32_t now_ms) const;
  bool token_matches_(const std::string &ticket_hex) const;
  bool bindings_match_(const uint8_t session_binding[SESSION_BINDING_BYTES],
                       uint32_t target_address,
                       const uint8_t candidate_sha256[SHA256_BYTES],
                       const char *target_version,
                       ActivationTicketFailure *failure) const;
  static bool copy_version_(std::array<char, VERSION_CAPACITY> *destination,
                            const char *source);
  void destroy_secret_(ActivationTicketState next_state,
                       ActivationTicketFailure failure);

  std::array<uint8_t, TICKET_BYTES> ticket_{};
  std::array<char, TICKET_BYTES * 2U + 1U> ticket_hex_{};
  std::array<uint8_t, SESSION_BINDING_BYTES> session_binding_{};
  std::array<uint8_t, SHA256_BYTES> candidate_sha256_{};
  std::array<char, VERSION_CAPACITY> target_version_{};
  ActivationTicketState state_{ActivationTicketState::EMPTY};
  ActivationTicketFailure failure_{ActivationTicketFailure::NONE};
  uint32_t started_ms_{0};
  uint32_t target_address_{0};
};

}  // namespace esphome::usp_ota_guard
