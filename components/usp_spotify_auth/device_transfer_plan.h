#pragma once
#include "device_catalog.h"
#include "control_scope.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace usp_spotify_devices {
constexpr uint32_t CATALOG_TTL_MS = 15000;
constexpr uint32_t TRANSFER_TTL_MS = 3000;

enum class Phase : uint8_t { IDLE, QUEUED, IN_FLIGHT };
enum class Outcome : uint8_t { NONE, CONFIRMED, REAUTH, FORBIDDEN, RATE_LIMIT, RETRY };

struct TransferRequest {
  uint64_t ticket{0};
  uint32_t generation{0};
  uint32_t catalog_revision{0};
  uint32_t expires_at{0};
  std::array<char, MAX_DEVICE_ID + 1> device_id{};
};

class TransferPlan {
 public:
  void session(uint32_t generation, std::string_view granted_scope) {
    generation_ = generation;
    read_scope_ = usp_spotify_controls::has_scope(granted_scope, "user-read-playback-state");
    modify_scope_ = usp_spotify_controls::has_scope(granted_scope, "user-modify-playback-state");
    catalog_ = {}; updated_at_ = 0; ++revision_; reset_flight_(); outcome_ = Outcome::NONE;
  }

  bool install(uint32_t generation, const Catalog &catalog, uint32_t now) {
    if (generation != generation_ || !read_scope_ || !catalog.valid) return false;
    catalog_ = catalog; updated_at_ = now; ++revision_; reset_flight_();
    outcome_ = Outcome::NONE; return true;
  }

  void transfer_action(uint32_t generation, bool known, bool allowed) {
    if (generation != generation_) return;
    transfer_action_known_ = known; transfer_action_allowed_ = allowed;
  }

  uint64_t request(uint32_t generation, uint8_t index, uint32_t now) {
    if (phase_ != Phase::IDLE || !eligible_(generation, index, now)) return 0;
    const auto &device = catalog_.devices[index];
    TransferRequest request{};
    request.ticket = next_ticket_++; request.generation = generation_;
    request.catalog_revision = revision_; request.expires_at = now + TRANSFER_TTL_MS;
    std::memcpy(request.device_id.data(), device.id.data(), device.id.size());
    queued_ = request; phase_ = Phase::QUEUED; outcome_ = Outcome::NONE;
    return request.ticket;
  }

  bool take(uint32_t generation, uint32_t now, TransferRequest *out) {
    if (!out || phase_ != Phase::QUEUED || !current_(generation, queued_, now)) {
      if (phase_ == Phase::QUEUED) reset_flight_();
      return false;
    }
    loan_ = queued_; queued_ = {}; phase_ = Phase::IN_FLIGHT; *out = loan_; return true;
  }

  bool current(uint32_t generation, const TransferRequest &request, uint32_t now) const {
    return phase_ == Phase::IN_FLIGHT && same_(loan_, request) && current_(generation, request, now);
  }

  bool finish(const TransferRequest &request, int status, bool complete) {
    if (phase_ != Phase::IN_FLIGHT || !same_(loan_, request)) return false;
    outcome_ = classify_(status, complete); reset_flight_(); return true;
  }

  const Catalog &catalog() const { return catalog_; }
  uint32_t revision() const { return revision_; }
  Phase phase() const { return phase_; }
  Outcome outcome() const { return outcome_; }

 private:
  bool fresh_(uint32_t now) const {
    return catalog_.valid && uint32_t(now - updated_at_) < CATALOG_TTL_MS;
  }
  bool action_ok_() const { return !transfer_action_known_ || transfer_action_allowed_; }
  bool eligible_(uint32_t generation, uint8_t index, uint32_t now) const {
    if (generation != generation_ || !read_scope_ || !modify_scope_ || !fresh_(now) ||
        index >= catalog_.count || !action_ok_()) return false;
    const auto &device = catalog_.devices[index];
    return device.selectable && !device.active && !device.restricted && device.id[0];
  }
  const Device *find_(std::string_view id) const {
    for (uint8_t i = 0; i < catalog_.count; ++i)
      if (id == catalog_.devices[i].id.data()) return &catalog_.devices[i];
    return nullptr;
  }
  bool current_(uint32_t generation, const TransferRequest &request, uint32_t now) const {
    if (generation != generation_ || request.generation != generation_ ||
        request.catalog_revision != revision_ || int32_t(request.expires_at - now) <= 0 ||
        !modify_scope_ || !fresh_(now) || !action_ok_()) return false;
    const auto *device = find_(request.device_id.data());
    return device && device->selectable && !device->active && !device->restricted;
  }
  static bool same_(const TransferRequest &left, const TransferRequest &right) {
    return left.ticket == right.ticket && left.generation == right.generation &&
        left.catalog_revision == right.catalog_revision && left.expires_at == right.expires_at &&
        left.device_id == right.device_id;
  }
  static Outcome classify_(int status, bool complete) {
    if (complete && status == 204) return Outcome::CONFIRMED;
    if (status == 401) return Outcome::REAUTH;
    if (status == 403) return Outcome::FORBIDDEN;
    if (status == 429) return Outcome::RATE_LIMIT;
    return Outcome::RETRY;
  }
  void reset_flight_() { queued_ = {}; loan_ = {}; phase_ = Phase::IDLE; }

  Catalog catalog_{};
  TransferRequest queued_{}, loan_{};
  uint64_t next_ticket_{1};
  uint32_t generation_{0}, revision_{0}, updated_at_{0};
  Phase phase_{Phase::IDLE};
  Outcome outcome_{Outcome::NONE};
  bool read_scope_{false}, modify_scope_{false};
  bool transfer_action_known_{false}, transfer_action_allowed_{false};
};
static_assert(sizeof(TransferPlan) <= 2800, "Transfer policy must remain bounded");
}  // namespace usp_spotify_devices
