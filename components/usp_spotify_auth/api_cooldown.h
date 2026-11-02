#pragma once
#include <algorithm>
#include <cstdint>

namespace usp_spotify_controls {
/** One Player API deadline shared by GET and controls, never reset on logout. */
class ApiCooldown {
 public:
  bool ready(uint32_t now) {
    if(active_ && int32_t(now-deadline_)>=0)active_=false;
    return !suspended_ && !active_;
  }
  void observe(int status,bool attempted,bool known,bool excessive,
               uint32_t seconds,uint32_t now) {
    if(!attempted || status!=429)return;
    if(excessive || (known && seconds>86400U)) {suspended_=true;return;}
    const uint32_t delay=(known?std::max<uint32_t>(1,seconds):60U)*1000U;
    if(!active_ || int32_t(now-deadline_)>=0 ||
        uint32_t(deadline_-now)<delay)deadline_=now+delay;
    active_=true;
  }
 private:
  uint32_t deadline_{0};
  bool active_{false},suspended_{false};
};
}  // namespace usp_spotify_controls
