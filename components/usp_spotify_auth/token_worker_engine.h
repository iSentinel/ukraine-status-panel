#pragma once
#include "usp_spotify_tokens.h"
#include "session_persistence.h"

namespace usp_spotify_tokens {
enum class Phase : uint8_t { IDLE, EXCHANGING, CONNECTED, REAUTH };
enum class Operation : uint8_t { NONE, CODE, REFRESH };

/** The adapter implements these operations with try-lock and an atomic epoch. */
class Bridge {
 public:
  virtual ~Bridge() = default;
  virtual uint32_t generation() const = 0;
  virtual bool take(usp_spotify_auth_return::TokenRequest *, uint32_t *, uint32_t *) = 0;
  virtual bool publish(uint32_t generation, Phase phase) = 0;
};
class Clock {
 public:
  virtual ~Clock() = default;
  virtual uint32_t now() const = 0;
};
struct HttpResult {
  bool attempted{false}, complete{false};
  int status{0};
  size_t size{0};
  bool retry_valid{false}, retry_excess{false};
  uint32_t retry_ms{0};
};
/** A concrete implementation owns TLS admission, fixed URL and bounded timeout. */
class Transport {
 public:
  virtual ~Transport() = default;
  virtual HttpResult perform(const char *, size_t, char *, size_t) = 0;
};

/** Provider cooldown survives cancellation and new authorization generations. */
class Cooldown {
 public:
  bool ready(uint32_t now) const {
    return !suspended_ && (!active_ || int32_t(now - deadline_) >= 0);
  }
  void wait(uint32_t now, uint32_t delay) {
    const uint32_t bounded_delay = std::min<uint32_t>(delay, 86400000U);
    if (!active_ || int32_t(now - deadline_) >= 0 ||
        uint32_t(deadline_ - now) < bounded_delay) deadline_ = now + bounded_delay;
    active_ = true;
  }
  void response(uint32_t now, const HttpResult &result) {
    if (result.status != 429) return;
    if (result.retry_excess) suspended_ = true;
    this->wait(now, result.retry_valid ? std::max<uint32_t>(60000U,result.retry_ms) : 60000U);
  }

 private:
  bool active_{false}, suspended_{false};
  uint32_t deadline_{0};
};

/** Policy runs only in a worker task; it never waits for the main/HTTP mutex. */
class Engine {
 public:
  Engine() = default;
  Engine(const Engine &) = delete;
  Engine &operator=(const Engine &) = delete;
  ~Engine() { this->reset_private_(); }

  void clear() { this->reset_private_(); initialized_ = false; }

  /** Worker-only bearer view; valid only until the next token-engine mutation. */
  std::string_view access(uint32_t now) const { return store_.access(now); }
  /** Worker-only granted scope view; never copied to UI or web responses. */
  std::string_view scope(uint32_t now) const { return store_.valid(now) ? store_.scope() : ""; }
  bool has_modify_scope(uint32_t now) const { return store_.has_modify_scope(now); }


  /** Worker-only token ownership; an old bearer must never label a new session. */
  bool current(uint32_t generation) const {
    return initialized_ && generation_ == generation;
  }
  uint32_t generation() const { return generation_; }

  /** A Player API 401 invalidates RAM and the persisted refresh session. */
  void force_reauth(usp_spotify_session::PersistenceBridge *persistence=nullptr) {
    this->clear_exchange_(); store_.clear();
    persist_pending_=false;
    if (persistence && !persistence->clear(generation_)) {
      clear_pending_=true; persistence_retry_at_=0;
    }
    this->schedule_(Phase::REAUTH);
  }

  void step(Bridge &bridge, Transport &transport, const Clock &clock,
            usp_spotify_session::PersistenceBridge *persistence=nullptr) {
    const auto current = bridge.generation();
    if (!initialized_ || generation_ != current) {
      this->reset_private_(); generation_ = current; initialized_ = true;
    }
    this->persistence_step_(bridge,persistence,clock.now());
    if (publication_pending_ && pending_phase_ == Phase::CONNECTED && !store_.valid(clock.now()))
      pending_phase_ = Phase::EXCHANGING;
    if (publication_pending_) {
      if (!bridge.publish(generation_, pending_phase_)) return;
      published_phase_ = pending_phase_; publication_pending_ = false;
    }
    if (operation_ != Operation::NONE) {
      this->execute_(bridge, transport, clock, persistence); return;
    }
    if (!store_.refresh().empty() && store_.refresh_due(clock.now())) {
      this->prepare_refresh_(clock.now()); return;
    }
    uint32_t taken_generation = 0, accepted_at = 0;
    if (!bridge.take(&request_, &taken_generation, &accepted_at)) return;
    if (taken_generation != bridge.generation() || taken_generation != generation_) {
      request_.clear(); return;
    }
    if (!code_form(request_, &form_)) {
      request_.clear(); this->schedule_(Phase::REAUTH); return;
    }
    operation_ = Operation::CODE; operation_started_ = accepted_at;
    this->schedule_(Phase::EXCHANGING);
  }

 private:
  void schedule_(Phase phase) { pending_phase_ = phase; publication_pending_ = true; }
  void clear_exchange_() {
    request_.clear(); form_.clear(); reply_.clear();
    usp_spotify_pkce::zeroize(body_.data(), body_.size());
    operation_ = Operation::NONE; operation_started_ = 0;
  }
  void reset_private_() {
    this->clear_exchange_(); store_.clear();restore_session_.clear(); publication_pending_ = false;
    restore_checked_=false;persist_pending_=false;clear_pending_=false;persistence_retry_at_=0;
    published_phase_ = Phase::IDLE;
  }
  void prepare_refresh_(uint32_t now) {
    if (!store_.valid(now) && published_phase_ == Phase::CONNECTED)
      this->schedule_(Phase::EXCHANGING);
    if (!cooldown_.ready(now)) return;
    if (!refresh_form(store_.client(), store_.refresh(), &form_)) {
      store_.clear(); this->schedule_(Phase::REAUTH); return;
    }
    operation_ = Operation::REFRESH; operation_started_ = now;
  }
  void execute_(Bridge &bridge, Transport &transport, const Clock &clock,
                usp_spotify_session::PersistenceBridge *persistence) {
    if (operation_ == Operation::CODE &&
        uint32_t(clock.now() - operation_started_) >= usp_spotify_auth_return::GRANT_TTL_MS) {
      this->clear_exchange_(); this->schedule_(Phase::REAUTH); return;
    }
    if (!cooldown_.ready(clock.now())) return;
    const uint32_t started = clock.now();
    usp_spotify_pkce::zeroize(body_.data(), body_.size());
    const HttpResult result = transport.perform(form_.data(),form_.size(),body_.data(),MAX_BODY);
    const uint32_t completed = clock.now();
    cooldown_.response(completed,result);
    if (bridge.generation() != generation_) { this->reset_private_(); return; }
    if (!result.attempted) {
      usp_spotify_pkce::zeroize(body_.data(), body_.size());
      cooldown_.wait(completed,500); return;
    }
    const bool refreshing = operation_ == Operation::REFRESH;
    const bool parsed = result.complete && result.size <= MAX_BODY &&
        ReplyParser({body_.data(), result.size}).parse(&reply_);
    const auto client = refreshing ? store_.client() : bounded(request_.client_id);
    if (result.status == 200 && parsed && store_.install(reply_,client,started,completed,refreshing)) {
      const bool changed=!refreshing || (reply_.fields & (8U|16U));
      if (changed) this->persist_(bridge,persistence,completed);
      this->schedule_(Phase::CONNECTED);
    } else this->failure_(bridge,persistence,refreshing,parsed,result,completed);
    // Once perform has been attempted, a code is never automatically sent again.
    this->clear_exchange_();
  }
  void failure_(Bridge &bridge,usp_spotify_session::PersistenceBridge *persistence,
                bool refreshing,bool parsed,const HttpResult &result,uint32_t now) {
    if (!refreshing || result.status == 400 || result.status == 401 || result.status == 403 ||
        (parsed && bounded(reply_.error) == "invalid_grant")) {
      store_.clear(); persist_pending_=false;
      if (persistence && bridge.generation()==generation_ && !persistence->clear(generation_)) {
        clear_pending_=true; persistence_retry_at_=now+PERSIST_RETRY_MS;
      }
      this->schedule_(Phase::REAUTH); return;
    }
    cooldown_.wait(now,30000);
    this->schedule_(store_.valid(now) ? Phase::CONNECTED : Phase::EXCHANGING);
  }
  static constexpr uint32_t PERSIST_RETRY_MS=30000U;
  static bool due_(uint32_t now,uint32_t deadline) {return int32_t(now-deadline)>=0;}
  void persistence_step_(Bridge &bridge,usp_spotify_session::PersistenceBridge *persistence,uint32_t now) {
    if(!persistence)return;
    if(clear_pending_) {
      if(!persistence_retry_at_ || due_(now,persistence_retry_at_)) {
        if(persistence->clear(generation_)) {clear_pending_=false;persistence_retry_at_=0;}
        else persistence_retry_at_=now+PERSIST_RETRY_MS;
      }
      return;
    }
    if(!restore_checked_) {
      restore_checked_=true;auto &session=restore_session_;session.clear();
      const auto state=persistence->load(generation_,&session);
      if(state==usp_spotify_session::LoadState::READY) {
        if(store_.restore(session.refresh.data(),session.client.data(),session.scope.data(),now))
          this->schedule_(Phase::EXCHANGING);
        else {persistence->clear(generation_);this->schedule_(Phase::REAUTH);}
      } else if(state==usp_spotify_session::LoadState::CORRUPT) {
        if(!persistence->clear(generation_)) {clear_pending_=true;persistence_retry_at_=now+PERSIST_RETRY_MS;}
        this->schedule_(Phase::REAUTH);
      } else if(state==usp_spotify_session::LoadState::KEY_UNAVAILABLE ||
                state==usp_spotify_session::LoadState::IO_ERROR) this->schedule_(Phase::REAUTH);
      session.clear();return;
    }
    if(persist_pending_ && due_(now,persistence_retry_at_)) this->persist_(bridge,persistence,now);
  }
  void persist_(Bridge &bridge,usp_spotify_session::PersistenceBridge *persistence,uint32_t now) {
    if(!persistence || bridge.generation()!=generation_ || store_.refresh().empty())return;
    if(persistence->save(generation_,store_.refresh(),store_.client(),store_.scope())) {
      persist_pending_=false;persistence_retry_at_=0;
    } else {persist_pending_=true;persistence_retry_at_=now+PERSIST_RETRY_MS;}
  }
  usp_spotify_session::Session restore_session_;
  usp_spotify_auth_return::TokenRequest request_;
  Form form_;
  Reply reply_;
  Store store_;
  std::array<char, MAX_BODY + 1> body_{};
  Cooldown cooldown_;
  uint32_t generation_{0}, operation_started_{0}, persistence_retry_at_{0};
  bool initialized_{false}, publication_pending_{false};
  bool restore_checked_{false},persist_pending_{false},clear_pending_{false};
  Phase pending_phase_{Phase::IDLE}, published_phase_{Phase::IDLE};
  Operation operation_{Operation::NONE};
};
static_assert(sizeof(Engine) <= 65536, "Worker context must fit the strict PSRAM budget");
}  // namespace usp_spotify_tokens
