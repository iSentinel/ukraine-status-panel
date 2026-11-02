#pragma once
#include "control_actions.h"
#include "control_scope.h"
#include "playback_model.h"
#include <cstdint>
#include <mutex>

namespace usp_spotify_value_controls {
constexpr uint32_t FACTS_TTL_MS = 10000;
constexpr uint32_t REQUEST_TTL_MS = 3000;

enum class Kind : uint8_t { VOLUME, SEEK };
enum class Phase : uint8_t { IDLE, QUEUED, IN_FLIGHT };
enum class Outcome : uint8_t { NONE, CONFIRMED, REAUTH, FORBIDDEN, RATE_LIMIT, RETRY };
struct SeekPermission { bool known{false}; bool allowed{false}; };

struct Facts {
  uint32_t updated_at{0};
  uint32_t duration_ms{0};
  bool valid{false};
  bool active{false};
  bool restricted{true};
  bool supports_volume{false};
  bool has_item{false};
  SeekPermission seek{};
};

struct Request {
  uint64_t ticket{0};
  uint32_t generation{0};
  uint32_t facts_revision{0};
  uint32_t expires_at{0};
  uint32_t value{0};
  Kind kind{Kind::VOLUME};
};

inline SeekPermission seek_permission(JsonVariantConst value) {
  if (!value.is<JsonObjectConst>()) return {};
  const JsonObjectConst actions = value.as<JsonObjectConst>();
  const bool supplied = usp_spotify_controls::contains(actions, "disallows");
  const bool nested = actions["disallows"].is<JsonObjectConst>();
  if (supplied && !nested) return {};
  const JsonObjectConst restrictions = nested ? actions["disallows"].as<JsonObjectConst>() : actions;
  bool allowed = false, flat = true;
  if (!usp_spotify_controls::restriction(restrictions, "seeking", nested, &allowed)) return {};
  if (nested && !usp_spotify_controls::restriction(actions, "seeking", true, &flat)) return {};
  return {nested || usp_spotify_controls::contains(restrictions, "seeking"), allowed && flat};
}

inline Facts playback_facts(const usp_spotify_playback::PlaybackSnapshot &snapshot,
                            JsonVariantConst actions, uint32_t updated_at) {
  Facts out{};
  out.updated_at = updated_at; out.duration_ms = snapshot.duration_ms;
  out.valid = snapshot.valid; out.active = snapshot.device_active;
  out.restricted = snapshot.restricted; out.supports_volume = snapshot.supports_volume;
  out.has_item = snapshot.has_item; out.seek = seek_permission(actions);
  return out;
}

class Plan {
 public:
  void session(uint32_t generation, std::string_view granted_scope) {
    std::lock_guard<std::mutex> lock(mutex_);
    generation_ = generation;
    modify_scope_ = usp_spotify_controls::has_scope(granted_scope, "user-modify-playback-state");
    facts_ = {}; ++facts_revision_; reset_flight_(); outcome_ = Outcome::NONE;
  }
  void facts(uint32_t generation, const Facts &facts) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation != generation_) return;
    facts_ = facts; ++facts_revision_;
    if (phase_ == Phase::QUEUED) reset_flight_();
  }
  uint64_t request_volume(uint32_t generation, uint32_t percent, uint32_t now) {
    return request_(generation, Kind::VOLUME, percent, now);
  }
  uint64_t request_seek(uint32_t generation, uint32_t position_ms, uint32_t now) {
    return request_(generation, Kind::SEEK, position_ms, now);
  }
  bool take(uint32_t generation, uint32_t now, Request *out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!out || phase_ != Phase::QUEUED || !current_(generation, queued_, now)) {
      if (phase_ == Phase::QUEUED) reset_flight_();
      return false;
    }
    loan_ = queued_; queued_ = {}; phase_ = Phase::IN_FLIGHT; *out = loan_; return true;
  }
  bool current(uint32_t generation, const Request &request, uint32_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    return phase_ == Phase::IN_FLIGHT && same_(loan_, request) && current_(generation, request, now);
  }
  bool finish(const Request &request, int status, bool complete) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (phase_ != Phase::IN_FLIGHT || !same_(loan_, request)) return false;
    outcome_ = classify_(status, complete); reset_flight_(); return true;
  }
  Phase phase() const { return phase_; }
  Outcome outcome() const { return outcome_; }
 private:
  uint64_t request_(uint32_t generation, Kind kind, uint32_t value, uint32_t now) {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock() || phase_ != Phase::IDLE || !allowed_(generation, kind, value, now)) return 0;
    Request request{}; request.ticket = next_ticket_++; request.generation = generation_;
    request.facts_revision = facts_revision_; request.expires_at = now + REQUEST_TTL_MS;
    request.kind = kind; request.value = value;
    queued_ = request; phase_ = Phase::QUEUED; outcome_ = Outcome::NONE; return request.ticket;
  }
  bool fresh_(uint32_t now) const { return facts_.valid && uint32_t(now - facts_.updated_at) < FACTS_TTL_MS; }
  bool value_allowed_(Kind kind, uint32_t value) const {
    if (kind == Kind::VOLUME) return value <= 100 && facts_.supports_volume;
    return value > 0 && facts_.has_item && facts_.duration_ms > 0 && value < facts_.duration_ms &&
        facts_.seek.known && facts_.seek.allowed;
  }
  bool allowed_(uint32_t generation, Kind kind, uint32_t value, uint32_t now) const {
    return generation == generation_ && modify_scope_ && fresh_(now) && facts_.active &&
        !facts_.restricted && value_allowed_(kind, value);
  }
  bool current_(uint32_t generation, const Request &request, uint32_t now) const {
    return generation == generation_ && request.generation == generation_ &&
        request.facts_revision == facts_revision_ && int32_t(request.expires_at - now) > 0 &&
        allowed_(generation, request.kind, request.value, now);
  }
  static bool same_(const Request &a, const Request &b) {
    return a.ticket == b.ticket && a.generation == b.generation && a.facts_revision == b.facts_revision &&
        a.expires_at == b.expires_at && a.value == b.value && a.kind == b.kind;
  }
  static Outcome classify_(int status, bool complete) {
    if (complete && status == 204) return Outcome::CONFIRMED;
    if (status == 401) return Outcome::REAUTH;
    if (status == 403) return Outcome::FORBIDDEN;
    if (status == 429) return Outcome::RATE_LIMIT;
    return Outcome::RETRY;
  }
  void reset_flight_() { queued_ = {}; loan_ = {}; phase_ = Phase::IDLE; }
  mutable std::mutex mutex_;
  Facts facts_{}; Request queued_{}, loan_{};
  uint64_t next_ticket_{1}; uint32_t generation_{0}, facts_revision_{0};
  Phase phase_{Phase::IDLE}; Outcome outcome_{Outcome::NONE}; bool modify_scope_{false};
};
static_assert(sizeof(Plan) <= 256, "Volume/seek plan must stay bounded metadata");
}  // namespace usp_spotify_value_controls
