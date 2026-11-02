#pragma once
#include <cstdint>
namespace usp_alarm_audio {
constexpr uint32_t REPEAT_MS=6000;
struct State { bool active{false}; bool enabled{true}; uint32_t next_at{0}; };
inline bool due(const State &s,uint32_t now) {
  return s.active && s.enabled && static_cast<int32_t>(now-s.next_at)>=0;
}
inline void armed(State *s,bool active,bool enabled,uint32_t now) {
  if(!s)return;
  const bool newly_active=active && !s->active;
  s->active=active;s->enabled=enabled;
  if(!active || !enabled)s->next_at=0;
  else if(newly_active || s->next_at==0)s->next_at=now;
}
inline void played(State *s,uint32_t now) {
  if(s && s->active && s->enabled)s->next_at=now+REPEAT_MS;
}
}
