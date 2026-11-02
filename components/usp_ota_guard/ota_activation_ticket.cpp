#include "ota_activation_ticket.h"

#include <cstring>

#include <esp_random.h>
#include <sodium.h>

namespace esphome::usp_ota_guard {
namespace {
constexpr char HEX_DIGITS[] = "0123456789abcdef";
}

const char *OtaActivationTicket::state_name(ActivationTicketState state) {
  switch (state) {
    case ActivationTicketState::EMPTY: return "empty";
    case ActivationTicketState::ACTIVE: return "active";
    case ActivationTicketState::CONSUMED: return "consumed";
    case ActivationTicketState::EXPIRED: return "expired";
  }
  return "unknown";
}

const char *OtaActivationTicket::failure_name(
    ActivationTicketFailure failure) {
  switch (failure) {
    case ActivationTicketFailure::NONE: return "none";
    case ActivationTicketFailure::INELIGIBLE_DECISION: return "ineligible_decision";
    case ActivationTicketFailure::INVALID_ARGUMENT: return "invalid_argument";
    case ActivationTicketFailure::NOT_ACTIVE: return "not_active";
    case ActivationTicketFailure::EXPIRED: return "expired";
    case ActivationTicketFailure::TOKEN_MISMATCH: return "token_mismatch";
    case ActivationTicketFailure::SESSION_MISMATCH: return "session_mismatch";
    case ActivationTicketFailure::TARGET_MISMATCH: return "target_mismatch";
    case ActivationTicketFailure::DIGEST_MISMATCH: return "digest_mismatch";
    case ActivationTicketFailure::VERSION_MISMATCH: return "version_mismatch";
  }
  return "unknown";
}

bool OtaActivationTicket::copy_version_(
    std::array<char, VERSION_CAPACITY> *destination, const char *source) {
  if (destination == nullptr || source == nullptr) return false;
  const size_t length = strnlen(source, destination->size());
  if (length == 0 || length >= destination->size()) return false;
  destination->fill('\0');
  std::memcpy(destination->data(), source, length);
  return true;
}

void OtaActivationTicket::destroy_secret_(ActivationTicketState next_state,
                                          ActivationTicketFailure failure) {
  sodium_memzero(this->ticket_.data(), this->ticket_.size());
  sodium_memzero(this->ticket_hex_.data(), this->ticket_hex_.size());
  sodium_memzero(this->session_binding_.data(), this->session_binding_.size());
  sodium_memzero(this->candidate_sha256_.data(), this->candidate_sha256_.size());
  sodium_memzero(this->target_version_.data(), this->target_version_.size());
  this->state_ = next_state;
  this->failure_ = failure;
  this->started_ms_ = 0;
  this->target_address_ = 0;
}

void OtaActivationTicket::clear() {
  this->destroy_secret_(ActivationTicketState::EMPTY,
                        ActivationTicketFailure::NONE);
}

bool OtaActivationTicket::issue(
    const ActivationDecision &decision,
    const uint8_t session_binding[SESSION_BINDING_BYTES],
    const uint8_t candidate_sha256[SHA256_BYTES], const char *target_version,
    uint32_t now_ms) {
  this->clear();
  if (decision.eligibility != ActivationEligibility::ELIGIBLE ||
      decision.reason != ActivationBlockReason::NONE ||
      decision.target == nullptr) {
    this->failure_ = ActivationTicketFailure::INELIGIBLE_DECISION;
    return false;
  }
  if (session_binding == nullptr || candidate_sha256 == nullptr ||
      !copy_version_(&this->target_version_, target_version)) {
    this->failure_ = ActivationTicketFailure::INVALID_ARGUMENT;
    return false;
  }

  esp_fill_random(this->ticket_.data(), this->ticket_.size());
  for (size_t index = 0; index < this->ticket_.size(); ++index) {
    this->ticket_hex_[index * 2U] = HEX_DIGITS[this->ticket_[index] >> 4U];
    this->ticket_hex_[index * 2U + 1U] =
        HEX_DIGITS[this->ticket_[index] & 0x0FU];
  }
  this->ticket_hex_[this->ticket_.size() * 2U] = '\0';
  std::memcpy(this->session_binding_.data(), session_binding,
              this->session_binding_.size());
  std::memcpy(this->candidate_sha256_.data(), candidate_sha256,
              this->candidate_sha256_.size());
  this->target_address_ = decision.target->address;
  this->started_ms_ = now_ms;
  this->failure_ = ActivationTicketFailure::NONE;
  this->state_ = ActivationTicketState::ACTIVE;
  return true;
}

bool OtaActivationTicket::expired_(uint32_t now_ms) const {
  return this->state_ == ActivationTicketState::ACTIVE &&
         now_ms - this->started_ms_ >= TICKET_LIFETIME_MS;
}

void OtaActivationTicket::expire_if_needed(uint32_t now_ms) {
  if (!this->expired_(now_ms)) return;
  this->destroy_secret_(ActivationTicketState::EXPIRED,
                        ActivationTicketFailure::EXPIRED);
}

bool OtaActivationTicket::token_matches_(const std::string &ticket_hex) const {
  constexpr size_t expected_length = TICKET_BYTES * 2U;
  if (ticket_hex.size() != expected_length) return false;
  return sodium_memcmp(ticket_hex.data(), this->ticket_hex_.data(),
                       expected_length) == 0;
}

bool OtaActivationTicket::bindings_match_(
    const uint8_t session_binding[SESSION_BINDING_BYTES],
    uint32_t target_address, const uint8_t candidate_sha256[SHA256_BYTES],
    const char *target_version, ActivationTicketFailure *failure) const {
  if (session_binding == nullptr || candidate_sha256 == nullptr ||
      target_version == nullptr || failure == nullptr) {
    if (failure != nullptr) *failure = ActivationTicketFailure::INVALID_ARGUMENT;
    return false;
  }
  if (sodium_memcmp(session_binding, this->session_binding_.data(),
                    this->session_binding_.size()) != 0) {
    *failure = ActivationTicketFailure::SESSION_MISMATCH;
    return false;
  }
  if (target_address != this->target_address_) {
    *failure = ActivationTicketFailure::TARGET_MISMATCH;
    return false;
  }
  if (sodium_memcmp(candidate_sha256, this->candidate_sha256_.data(),
                    this->candidate_sha256_.size()) != 0) {
    *failure = ActivationTicketFailure::DIGEST_MISMATCH;
    return false;
  }
  const size_t expected_length = strnlen(this->target_version_.data(),
                                         this->target_version_.size());
  const size_t supplied_length = strnlen(target_version, VERSION_CAPACITY);
  if (expected_length == 0 || supplied_length != expected_length ||
      sodium_memcmp(target_version, this->target_version_.data(),
                    expected_length) != 0) {
    *failure = ActivationTicketFailure::VERSION_MISMATCH;
    return false;
  }
  *failure = ActivationTicketFailure::NONE;
  return true;
}

bool OtaActivationTicket::consume(
    const std::string &ticket_hex,
    const uint8_t session_binding[SESSION_BINDING_BYTES],
    uint32_t target_address, const uint8_t candidate_sha256[SHA256_BYTES],
    const char *target_version, uint32_t now_ms) {
  this->expire_if_needed(now_ms);
  if (this->state_ != ActivationTicketState::ACTIVE) {
    if (this->state_ != ActivationTicketState::EXPIRED)
      this->failure_ = ActivationTicketFailure::NOT_ACTIVE;
    return false;
  }
  if (!this->token_matches_(ticket_hex)) {
    this->failure_ = ActivationTicketFailure::TOKEN_MISMATCH;
    return false;
  }
  ActivationTicketFailure binding_failure = ActivationTicketFailure::NONE;
  if (!this->bindings_match_(session_binding, target_address, candidate_sha256,
                             target_version, &binding_failure)) {
    this->failure_ = binding_failure;
    return false;
  }

  this->destroy_secret_(ActivationTicketState::CONSUMED,
                        ActivationTicketFailure::NONE);
  return true;
}

std::string OtaActivationTicket::ticket_hex() const {
  if (this->state_ != ActivationTicketState::ACTIVE) return {};
  return std::string(this->ticket_hex_.data(), TICKET_BYTES * 2U);
}

uint32_t OtaActivationTicket::remaining_ms(uint32_t now_ms) const {
  if (this->state_ != ActivationTicketState::ACTIVE) return 0;
  const uint32_t elapsed = now_ms - this->started_ms_;
  return elapsed >= TICKET_LIFETIME_MS ? 0 : TICKET_LIFETIME_MS - elapsed;
}

ActivationTicketSnapshot OtaActivationTicket::snapshot() const {
  ActivationTicketSnapshot snapshot{};
  snapshot.state = this->state_;
  snapshot.failure = this->failure_;
  snapshot.started_ms = this->started_ms_;
  snapshot.target_address = this->target_address_;
  snapshot.ttl_ms = TICKET_LIFETIME_MS;
  snapshot.active = this->state_ == ActivationTicketState::ACTIVE;
  return snapshot;
}

}  // namespace esphome::usp_ota_guard
