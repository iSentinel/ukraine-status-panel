#pragma once

#include "playback_model.h"
#include "token_worker_engine.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace usp_spotify_playback_worker {

constexpr uint32_t PLAYING_POLL_MS = 5000;
constexpr uint32_t PAUSED_POLL_MS = 30000;
constexpr uint32_t IDLE_POLL_MS = 60000;
constexpr uint32_t RETRY_MS = 30000;
constexpr uint32_t NO_TRANSPORT_RETRY_MS = 500;
constexpr uint32_t RATE_LIMIT_FALLBACK_MS = 60000;
constexpr size_t BODY_BYTES = 32768;

/** Bounded read-only state copied out of the Spotify worker. */
struct PlaybackState {
  usp_spotify_playback::PlaybackSnapshot snapshot{};
  usp_spotify_playback::ResponseKind kind{usp_spotify_playback::ResponseKind::EMPTY};
  uint32_t updated_at{0};
};

class WorkerClock {
 public:
  virtual ~WorkerClock() = default;
  virtual uint32_t now() const = 0;
};

class WorkerBridge {
 public:
  virtual ~WorkerBridge() = default;
  virtual uint32_t generation() const = 0;
  virtual bool publish(uint32_t generation, const PlaybackState &state) = 0;
};

class WorkerTransport {
 public:
  virtual ~WorkerTransport() = default;
  virtual usp_spotify_tokens::HttpResult perform(
      std::string_view access_token, char *output, size_t capacity) = 0;
};

class WorkerParser {
 public:
  virtual ~WorkerParser() = default;
  virtual bool parse(const char *body, size_t size,
                     usp_spotify_playback::PlaybackSnapshot *output) = 0;
};

/**
 * Read-only Player policy. It owns no credentials and never performs a control call.
 * The caller supplies the current RAM token from the token worker on each step.
 */
class Worker {
 public:
  Worker() = default;
  Worker(const Worker &) = delete;
  Worker &operator=(const Worker &) = delete;
  ~Worker() { this->clear(); }

  /** Observe an explicitly attempted control without replaying the command. */
  void request_poll() { deadline_active_=false; }

  bool take_reauth() {
    const bool requested = reauth_requested_;
    reauth_requested_ = false;
    return requested;
  }

  void clear() {
    usp_spotify_pkce::zeroize(body_.data(), body_.size());
    generation_ = 0;
    deadline_ = 0;
    initialized_ = false;
    deadline_active_ = false;
    empty_published_ = false;
    reauth_requested_ = false;
  }

  void step(WorkerBridge &bridge, std::string_view access_token,
            WorkerTransport &transport, WorkerParser &parser,
            const WorkerClock &clock) {
    const uint32_t current = bridge.generation();
    if (!initialized_ || generation_ != current) {
      this->reset_generation_(current);
      this->publish_empty_(bridge, clock.now());
    }
    if (access_token.empty()) {
      this->publish_empty_(bridge, clock.now());
      return;
    }
    empty_published_ = false;
    if (!this->ready_(clock.now())) return;

    usp_spotify_pkce::zeroize(body_.data(), body_.size());
    const auto result = transport.perform(
        access_token, body_.data(), BODY_BYTES);
    const uint32_t completed = clock.now();
    if (bridge.generation() != generation_) {
      usp_spotify_pkce::zeroize(body_.data(), body_.size());
      this->reset_generation_(bridge.generation());
      return;
    }
    if (!result.attempted) {
      this->schedule_(completed, NO_TRANSPORT_RETRY_MS);
      this->publish_kind_(bridge, usp_spotify_playback::ResponseKind::RETRY,
                          completed);
      return;
    }
    this->handle_(bridge, parser, result, completed);
    usp_spotify_pkce::zeroize(body_.data(), body_.size());
  }

 private:
  bool ready_(uint32_t now) const {
    return !deadline_active_ || int32_t(now - deadline_) >= 0;
  }

  void schedule_(uint32_t now, uint32_t delay) {
    deadline_ = now + delay;
    deadline_active_ = true;
  }

  void reset_generation_(uint32_t generation) {
    usp_spotify_pkce::zeroize(body_.data(), body_.size());
    generation_ = generation;
    deadline_ = 0;
    deadline_active_ = false;
    empty_published_ = false;
    reauth_requested_ = false;
    initialized_ = true;
  }

  void publish_empty_(WorkerBridge &bridge, uint32_t now) {
    if (empty_published_) return;
    PlaybackState state{};
    state.kind = usp_spotify_playback::ResponseKind::EMPTY;
    state.updated_at = now;
    if (bridge.publish(generation_, state)) empty_published_ = true;
  }

  void publish_kind_(WorkerBridge &bridge,
                     usp_spotify_playback::ResponseKind kind,
                     uint32_t now) {
    PlaybackState state{};
    state.kind = kind;
    state.updated_at = now;
    bridge.publish(generation_, state);
  }

  void handle_(WorkerBridge &bridge, WorkerParser &parser,
               const usp_spotify_tokens::HttpResult &result,
               uint32_t now) {
    using usp_spotify_playback::ResponseKind;
    const auto kind = usp_spotify_playback::classify_status(result.status);
    if (kind == ResponseKind::JSON) {
      PlaybackState state{};
      state.kind = ResponseKind::JSON;
      state.updated_at = now;
      if (result.complete && result.size <= BODY_BYTES && result.size > 0 &&
          parser.parse(body_.data(), result.size, &state.snapshot)) {
        bridge.publish(generation_, state);
        this->schedule_(now, state.snapshot.is_playing
                                 ? PLAYING_POLL_MS
                                 : PAUSED_POLL_MS);
      } else {
        this->publish_kind_(bridge, ResponseKind::RETRY, now);
        this->schedule_(now, RETRY_MS);
      }
      return;
    }
    this->publish_kind_(bridge, kind, now);
    if (kind == ResponseKind::REAUTH) {
      reauth_requested_ = true;
      empty_published_ = true;
    }
    if (kind == ResponseKind::RATE_LIMIT) {
      const uint32_t delay = result.retry_excess ? 86400000U :
          (result.retry_valid ? std::max<uint32_t>(RATE_LIMIT_FALLBACK_MS,
                                                   result.retry_ms)
                              : RATE_LIMIT_FALLBACK_MS);
      this->schedule_(now, delay);
    } else if (kind == ResponseKind::EMPTY) {
      this->schedule_(now, IDLE_POLL_MS);
    } else if (kind == ResponseKind::REAUTH) {
      this->schedule_(now, PLAYING_POLL_MS);
    } else {
      this->schedule_(now, RETRY_MS);
    }
  }

  std::array<char, BODY_BYTES + 1> body_{};
  uint32_t generation_{0};
  uint32_t deadline_{0};
  bool initialized_{false};
  bool deadline_active_{false};
  bool empty_published_{false};
  bool reauth_requested_{false};
};

static_assert(sizeof(PlaybackState) <= 1100, "Playback state must stay bounded");
static_assert(sizeof(Worker) <= 34000, "Playback worker must fit PSRAM budget");

}  // namespace usp_spotify_playback_worker
