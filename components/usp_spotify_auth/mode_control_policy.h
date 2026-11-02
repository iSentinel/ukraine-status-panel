#pragma once
#include "control_actions.h"
#include "control_scope.h"
#include <cstdint>
#include <mutex>
#include <string_view>

namespace usp_spotify_modes {
constexpr uint32_t FACTS_TTL_MS = 10000;
constexpr uint32_t REQUEST_TTL_MS = 3000;

enum class Kind : uint8_t { SHUFFLE, REPEAT };
enum class Repeat : uint8_t { OFF, CONTEXT, TRACK };
enum class Phase : uint8_t { IDLE, QUEUED, IN_FLIGHT };
enum class Outcome : uint8_t { NONE, CONFIRMED, REAUTH, FORBIDDEN, RATE_LIMIT, RETRY };
struct Permission { bool known{false}; bool allowed{false}; };
struct Facts {
  uint32_t updated_at{0};
  bool valid{false}, active{false}, restricted{true}, shuffle{false};
  Repeat repeat{Repeat::OFF};
  Permission shuffle_permission{}, repeat_context{}, repeat_track{};
};
struct Request {
  uint64_t ticket{0};
  uint32_t generation{0}, facts_revision{0}, expires_at{0};
  Kind kind{Kind::SHUFFLE};
  bool shuffle{false};
  Repeat repeat{Repeat::OFF};
};

inline Permission permission(JsonVariantConst value, const char *name) {
  if (!value.is<JsonObjectConst>()) return {};
  const JsonObjectConst actions=value.as<JsonObjectConst>();
  const bool supplied=usp_spotify_controls::contains(actions,"disallows");
  const bool nested=actions["disallows"].is<JsonObjectConst>();
  if (supplied && !nested) return {};
  const JsonObjectConst restrictions=nested?actions["disallows"].as<JsonObjectConst>():actions;
  bool allowed=false,flat=true;
  if (!usp_spotify_controls::restriction(restrictions,name,nested,&allowed)) return {};
  if (nested && !usp_spotify_controls::restriction(actions,name,true,&flat)) return {};
  return {nested || usp_spotify_controls::contains(restrictions,name),allowed&&flat};
}

inline bool repeat_value(JsonVariantConst value, Repeat *out) {
  if (!out || !value.is<const char *>()) return false;
  const std::string_view text=value.as<const char *>();
  if (text=="off") *out=Repeat::OFF;
  else if (text=="context") *out=Repeat::CONTEXT;
  else if (text=="track") *out=Repeat::TRACK;
  else return false;
  return true;
}

inline bool parse_facts(JsonObjectConst root, uint32_t updated_at, Facts *out) {
  if (!out || root.isNull() || !root["shuffle_state"].is<bool>()) return false;
  Facts next{}; next.updated_at=updated_at; next.valid=true;
  next.shuffle=root["shuffle_state"].as<bool>();
  if (!repeat_value(root["repeat_state"],&next.repeat)) return false;
  JsonObjectConst device=root["device"].as<JsonObjectConst>();
  if (device.isNull() || !device["is_active"].is<bool>() || !device["is_restricted"].is<bool>()) return false;
  next.active=device["is_active"].as<bool>(); next.restricted=device["is_restricted"].as<bool>();
  const auto actions=root["actions"];
  next.shuffle_permission=permission(actions,"toggling_shuffle");
  next.repeat_context=permission(actions,"toggling_repeat_context");
  next.repeat_track=permission(actions,"toggling_repeat_track");
  *out=next; return true;
}

class Plan {
 public:
  void session(uint32_t generation,std::string_view scope) {
    std::lock_guard<std::mutex> lock(mutex_);
    generation_=generation;modify_scope_=usp_spotify_controls::has_scope(scope,"user-modify-playback-state");
    facts_={};++facts_revision_;reset_();outcome_=Outcome::NONE;
  }
  void facts(uint32_t generation,const Facts &facts) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation!=generation_) return;
    facts_=facts;++facts_revision_;if(phase_==Phase::QUEUED)reset_();
  }
  uint64_t request_shuffle_toggle(uint32_t generation,uint32_t now) {
    std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock() || phase_!=Phase::IDLE || !base_(generation,now) ||
       !facts_.shuffle_permission.known || !facts_.shuffle_permission.allowed) return 0;
    Request request=make_(generation,Kind::SHUFFLE,now);request.shuffle=!facts_.shuffle;
    queued_=request;phase_=Phase::QUEUED;outcome_=Outcome::NONE;return request.ticket;
  }
  uint64_t request_repeat(uint32_t generation,Repeat target,uint32_t now) {
    std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock() || phase_!=Phase::IDLE || !base_(generation,now) ||
       target==facts_.repeat || !repeat_allowed_(facts_.repeat,target)) return 0;
    Request request=make_(generation,Kind::REPEAT,now);request.repeat=target;
    queued_=request;phase_=Phase::QUEUED;outcome_=Outcome::NONE;return request.ticket;
  }
  bool take(uint32_t generation,uint32_t now,Request *out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!out || phase_!=Phase::QUEUED || !current_(generation,queued_,now)) {
      if(phase_==Phase::QUEUED) reset_();
      return false;
    }
    loan_=queued_;queued_={};phase_=Phase::IN_FLIGHT;*out=loan_;return true;
  }
  bool current(uint32_t generation,const Request &request,uint32_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    return phase_==Phase::IN_FLIGHT && same_(loan_,request) && current_(generation,request,now);
  }
  bool finish(const Request &request,int status,bool complete) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(phase_!=Phase::IN_FLIGHT || !same_(loan_,request))return false;
    outcome_=classify_(status,complete);reset_();return true;
  }
  Outcome outcome() const {return outcome_;}
 private:
  bool fresh_(uint32_t now) const {return facts_.valid && uint32_t(now-facts_.updated_at)<FACTS_TTL_MS;}
  bool base_(uint32_t generation,uint32_t now) const {
    return generation==generation_ && modify_scope_ && fresh_(now) && facts_.active && !facts_.restricted;
  }
  static bool permit_(const Permission &p){return p.known&&p.allowed;}
  bool repeat_allowed_(Repeat current,Repeat target) const {
    const bool leave=current==Repeat::CONTEXT?permit_(facts_.repeat_context):
                     current==Repeat::TRACK?permit_(facts_.repeat_track):true;
    const bool enter=target==Repeat::CONTEXT?permit_(facts_.repeat_context):
                     target==Repeat::TRACK?permit_(facts_.repeat_track):true;
    return leave&&enter;
  }
  Request make_(uint32_t generation,Kind kind,uint32_t now) {
    Request r{};r.ticket=next_ticket_++;r.generation=generation;r.facts_revision=facts_revision_;
    r.expires_at=now+REQUEST_TTL_MS;r.kind=kind;return r;
  }
  bool current_(uint32_t generation,const Request &request,uint32_t now) const {
    if(generation!=generation_ || request.generation!=generation_ || request.facts_revision!=facts_revision_ ||
       int32_t(request.expires_at-now)<=0 || !base_(generation,now))return false;
    if(request.kind==Kind::SHUFFLE)
      return permit_(facts_.shuffle_permission) && request.shuffle!=facts_.shuffle;
    return request.repeat!=facts_.repeat && repeat_allowed_(facts_.repeat,request.repeat);
  }
  static bool same_(const Request&a,const Request&b){return a.ticket==b.ticket&&a.generation==b.generation&&a.facts_revision==b.facts_revision&&a.expires_at==b.expires_at&&a.kind==b.kind&&a.shuffle==b.shuffle&&a.repeat==b.repeat;}
  static Outcome classify_(int status,bool complete){if(complete&&status==204)return Outcome::CONFIRMED;if(status==401)return Outcome::REAUTH;if(status==403)return Outcome::FORBIDDEN;if(status==429)return Outcome::RATE_LIMIT;return Outcome::RETRY;}
  void reset_(){queued_={};loan_={};phase_=Phase::IDLE;}
  mutable std::mutex mutex_;Facts facts_{};Request queued_{},loan_{};uint64_t next_ticket_{1};
  uint32_t generation_{0},facts_revision_{0};Phase phase_{Phase::IDLE};Outcome outcome_{Outcome::NONE};bool modify_scope_{false};
};
static_assert(sizeof(Plan)<=256,"Shuffle/repeat policy must stay bounded metadata");
}  // namespace usp_spotify_modes
