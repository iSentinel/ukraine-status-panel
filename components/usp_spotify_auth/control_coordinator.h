#pragma once
#include "api_cooldown.h"
#include "control_view.h"
#include <mutex>

namespace usp_spotify_controls {
constexpr uint32_t FACTS_TTL_MS=10000;

/** Only bounded permissions are shared with the UI; credentials stay in the worker. */
struct Facts {
  uint32_t updated_at{0};
  uint8_t allowed_mask{0};
  bool valid{false},active{false},restricted{true},playing{false};
};


/** Main uses try-lock; the worker never holds this metadata lock during HTTPS. */
class Coordinator {
 public:
  Coordinator()=default;
  Coordinator(const Coordinator &)=delete;
  Coordinator &operator=(const Coordinator &)=delete;

  /** Only a valid token with an actual modify scope enables a command session. */
  void session(uint32_t generation,bool authorized) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!initialized_ || generation!=generation_ || authorized!=authorized_) {
      if(authorized)authorized=plan_.start_session();
      else plan_.disconnect();
      facts_={};observation_=false;reauth_=false;
    }
    generation_=generation;authorized_=authorized;initialized_=true;
  }

  /** A failed or non-current GET cannot preserve stale command permissions. */
  void facts(uint32_t generation,const Facts &facts) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!initialized_ || generation!=generation_)return;
    facts_=facts;
  }

  /** UI submits one explicit tap, with no waiting and no burst queue. */
  uint64_t request(uint32_t generation,Command command,uint32_t now) {
    std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock() || !matching_(generation) || !cooldown_.ready(now))return 0;
    return plan_.request(command,capabilities_(now),now);
  }

  /** Choose Play/Pause from the same fresh facts used for command admission. */
  uint64_t request_play_pause(uint32_t generation,uint32_t now) {
    std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock() || !matching_(generation) || !cooldown_.ready(now))return 0;
    const auto command=facts_.playing?Command::PAUSE:Command::PLAY;
    return plan_.request(command,capabilities_(now),now);
  }

  /** The worker takes once and keeps an owned ticket while HTTPS is running. */
  bool take(uint32_t generation,uint32_t now,Request *request) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!request || !matching_(generation) || !cooldown_.ready(now) ||
        !plan_.take(now,capabilities_(now),request))return false;
    loan_=*request;loan_generation_=generation;return true;
  }

  /** Revalidate immediately before dispatch; no API or rendering lock is taken. */
  bool current(uint32_t generation,const Request &request,uint32_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto caps=capabilities_(now);
    const auto bit=static_cast<unsigned>(request.command);
    return matching_(generation) && same_loan_(request) && plan_.current(request) &&
        loan_generation_==generation_ && int32_t(request.expires_at-now)>0 && bit<4 &&
        cooldown_.ready(now) && caps.snapshot_fresh && caps.modify_scope &&
        caps.active_device && !caps.restricted && (caps.allowed_mask&(1U<<bit));
  }

  /** Every real Player429 is retained, including GET results from old sessions. */
  void provider_response(int status,bool attempted,bool known,bool excessive,
                         uint32_t seconds,uint32_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    cooldown_.observe(status,attempted,known,excessive,seconds,now);
    if(!cooldown_.ready(now) && plan_.phase()==Phase::QUEUED) {
      Request discarded{};
      plan_.take(now,Capabilities{},&discarded);
    }
  }

  /** Release only the matching flight; stale401 cannot invalidate a new token. */
  bool finish(uint32_t generation,const Request &request,
              const Response &response,uint32_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!same_loan_(request))return false;
    cooldown_.observe(response.status,response.attempted,response.retry_known,
                      response.retry_excessive,response.retry_seconds,now);
    if(!matching_(generation) && generation_==loan_generation_) {
      plan_.disconnect();authorized_=false;facts_={};
    }
    const bool accepted=plan_.finish(request.ticket,response,now);
    loan_={};
    if(accepted && response.attempted) {
      facts_={};observation_=true;
      reauth_=response.status==401;
      if(reauth_)authorized_=false;
    }
    return accepted;
  }

  bool api_ready(uint32_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    return cooldown_.ready(now);
  }

  /** A command reply schedules observation, never an automatic replay. */
  bool take_observation() {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool value=observation_;observation_=false;return value;
  }

  bool take_reauth() {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool value=reauth_;reauth_=false;return value;
  }

  /** UI reads only permission bits and status; try-lock skips a busy frame. */
  bool copy_view(uint32_t generation,uint32_t now,ControlView *view) {
    if(!view)return false;
    std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock())return false;
    *view={};
    if(generation!=generation_)return true;
    view->phase=plan_.phase();view->outcome=plan_.outcome();
    if(matching_(generation) && cooldown_.ready(now) &&
        plan_.phase()==Phase::IDLE) {
      const auto caps=capabilities_(now);
      if(caps.snapshot_fresh && caps.modify_scope && caps.active_device && !caps.restricted)
        view->allowed_mask=caps.allowed_mask;
    }
    return true;
  }

 private:
  bool matching_(uint32_t generation) const {
    return initialized_ && authorized_ && generation==generation_;
  }
  bool same_loan_(const Request &request) const {
    return plan_.phase()==Phase::IN_FLIGHT && loan_.ticket==request.ticket &&
        loan_.session==request.session && loan_.command==request.command &&
        loan_.expires_at==request.expires_at;
  }
  Capabilities capabilities_(uint32_t now) const {
    return {facts_.valid && uint32_t(now-facts_.updated_at)<FACTS_TTL_MS,
            authorized_,facts_.active,facts_.restricted,facts_.allowed_mask};
  }
  std::mutex mutex_;
  Plan plan_;
  ApiCooldown cooldown_;
  Facts facts_{};
  Request loan_{};
  uint32_t generation_{0},loan_generation_{0};
  bool initialized_{false},authorized_{false},observation_{false},reauth_{false};
};
static_assert(sizeof(Coordinator)<=256,"Command coordinator must remain bounded metadata");
}  // namespace usp_spotify_controls
