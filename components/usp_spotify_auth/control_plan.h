#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>

namespace usp_spotify_controls {
constexpr uint32_t QUEUE_TTL_MS = 2000;
constexpr uint32_t MAX_RETRY_SECONDS = 86400;
enum class Command : uint8_t { PLAY, PAUSE, NEXT, PREVIOUS };
enum class Method : uint8_t { NONE, PUT, POST };
enum class Phase : uint8_t { IDLE, QUEUED, IN_FLIGHT };
enum class Outcome : uint8_t {
  NONE, PENDING, SENT, ACCEPTED, EXPIRED, NOT_SENT,
  REAUTH, FORBIDDEN, UNAVAILABLE, RATE_LIMITED, UNCERTAIN
};
struct Endpoint { Method method; const char *path; };
struct Capabilities {
  bool snapshot_fresh{false};
  bool modify_scope{false};
  bool active_device{false};
  bool restricted{true};
  uint8_t allowed_mask{0};
};
struct Request {
  uint64_t ticket{0};
  uint64_t session{0};
  uint32_t expires_at{0};
  Command command{Command::PLAY};
};
struct Response {
  bool attempted{false};
  int status{0};
  bool retry_known{false};
  bool retry_excessive{false};
  uint32_t retry_seconds{0};
  bool complete{false};
};

/** Fixed active-device commands only. No arbitrary URL, query or context body. */
constexpr Endpoint endpoint(Command command) {
  switch (command) {
    case Command::PLAY: return {Method::PUT, "/v1/me/player/play"};
    case Command::PAUSE: return {Method::PUT, "/v1/me/player/pause"};
    case Command::NEXT: return {Method::POST, "/v1/me/player/next"};
    case Command::PREVIOUS: return {Method::POST, "/v1/me/player/previous"};
  }
  return {Method::NONE, nullptr};
}

/**
 * Pure decision state; caller serializes access outside HTTPS.
 * A dispatched request keeps its single slot until completion, including logout.
 * No automatic replay exists. HTTP204 means accepted, not observed playback.
 */
class Plan {
 public:
  Plan() = default;
  Plan(const Plan &) = delete;
  Plan &operator=(const Plan &) = delete;
  Phase phase() const { return phase_; }
  Outcome outcome() const { return outcome_; }
  /** Revalidation must match the active session, even while an old flight is retained. */
  bool current(const Request &request) const {
    return phase_==Phase::IN_FLIGHT && session_!=0 &&
        request.session==session_ && request.ticket==request_.ticket;
  }


  /** Mint a non-reusable local session; old in-flight HTTPS retains its slot. */
  bool start_session() {
    this->disconnect();
    if (session_serial_ == std::numeric_limits<uint64_t>::max()) return false;
    session_ = ++session_serial_;
    return true;
  }

  /** Clear queued work immediately, but do not pretend active HTTPS ended. */
  void disconnect() {
    session_ = 0;
    outcome_ = Outcome::NONE;
    if (phase_ == Phase::QUEUED) this->release_();
  }

  /** Admit exactly one fresh explicit user action, never a burst queue. */
  uint64_t request(Command command, const Capabilities &caps, uint32_t now) {
    const auto bit = static_cast<uint8_t>(command);
    if (bit > 3 || !session_ || phase_ != Phase::IDLE || this->blocked(now)) return 0;
    if (!permitted_(command, caps)) return 0;
    if (ticket_serial_ == std::numeric_limits<uint64_t>::max()) return 0;
    request_ = {++ticket_serial_, session_, now + QUEUE_TTL_MS, command};
    phase_ = Phase::QUEUED;
    outcome_ = Outcome::PENDING;
    return request_.ticket;
  }

  /** Take a queued command once; an expired tap never runs unexpectedly later. */
  bool take(uint32_t now, const Capabilities &caps, Request *output) {
    if (!output || phase_ != Phase::QUEUED) return false;
    if (due_(now, request_.expires_at)) {
      this->release_();
      outcome_ = Outcome::EXPIRED;
      return false;
    }
    if (!permitted_(request_.command, caps)) {
      this->release_();
      outcome_ = Outcome::UNAVAILABLE;
      return false;
    }
    *output = request_;
    phase_ = Phase::IN_FLIGHT;
    outcome_ = Outcome::SENT;
    return true;
  }

  /** Finish only the matching flight; stale replies cannot update the new UI. */
  bool finish(uint64_t ticket, const Response &reply, uint32_t now) {
    if (phase_ != Phase::IN_FLIGHT || request_.ticket != ticket) return false;
    const bool current = session_ != 0 && request_.session == session_;
    if (reply.attempted && reply.status == 429) this->rate_limit_(reply, now);
    outcome_ = current ? classify_(reply) : Outcome::NONE;
    this->release_();
    return current;
  }

  /** Keep provider cooldown across logout; excessive Retry-After fails closed. */
  bool blocked(uint32_t now) {
    if (rate_excessive_) return true;
    if (rate_active_ && due_(now, rate_deadline_)) rate_active_ = false;
    return rate_active_;
  }

 private:
  static bool permitted_(Command command, const Capabilities &caps) {
    const auto bit = static_cast<uint8_t>(command);
    return bit < 4 && caps.snapshot_fresh && caps.modify_scope &&
           caps.active_device && !caps.restricted && (caps.allowed_mask & (1U << bit));
  }

  static bool due_(uint32_t now, uint32_t deadline) {
    return static_cast<int32_t>(now - deadline) >= 0;
  }

  static Outcome classify_(const Response &reply) {
    if (!reply.attempted) return Outcome::NOT_SENT;
    if (reply.status == 204) return reply.complete ? Outcome::ACCEPTED : Outcome::UNCERTAIN;
    if (reply.status == 401) return Outcome::REAUTH;
    if (reply.status == 403) return Outcome::FORBIDDEN;
    if (reply.status == 404) return Outcome::UNAVAILABLE;
    if (reply.status == 429) return Outcome::RATE_LIMITED;
    return Outcome::UNCERTAIN;
  }

  void release_() {
    request_ = {};
    phase_ = Phase::IDLE;
  }

  void rate_limit_(const Response &reply, uint32_t now) {
    if (reply.retry_excessive ||
        (reply.retry_known && reply.retry_seconds > MAX_RETRY_SECONDS)) {
      rate_excessive_ = true;
      return;
    }
    uint32_t seconds = reply.retry_known ? reply.retry_seconds : 60;
    if (!seconds) seconds = 1;
    rate_deadline_ = now + seconds * 1000;
    rate_active_ = true;
  }

  Request request_{};
  uint64_t session_serial_{0};
  uint64_t session_{0};
  uint64_t ticket_serial_{0};
  uint32_t rate_deadline_{0};
  Phase phase_{Phase::IDLE};
  Outcome outcome_{Outcome::NONE};
  bool rate_active_{false};
  bool rate_excessive_{false};
};
static_assert(sizeof(Plan) <= 96, "Command policy must remain bounded metadata");
}  // namespace usp_spotify_controls
