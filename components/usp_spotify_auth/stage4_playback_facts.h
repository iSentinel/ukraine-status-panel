#pragma once
#include "playback_model.h"
#include "value_control_policy.h"
#include "mode_control_policy.h"
#include "queue_policy.h"

namespace usp_spotify_stage4_binding {
inline usp_spotify_value_controls::Facts value_facts(
    const usp_spotify_playback::PlaybackSnapshot &s,uint32_t updated_at) {
  return {updated_at,s.duration_ms,s.valid,s.device_active,s.restricted,
          s.supports_volume,s.has_item,{s.seek.known,s.seek.allowed}};
}
inline usp_spotify_modes::Repeat repeat_value(usp_spotify_playback::RepeatState value) {
  switch(value) {
    case usp_spotify_playback::RepeatState::CONTEXT:return usp_spotify_modes::Repeat::CONTEXT;
    case usp_spotify_playback::RepeatState::TRACK:return usp_spotify_modes::Repeat::TRACK;
    default:return usp_spotify_modes::Repeat::OFF;
  }
}
inline usp_spotify_modes::Facts mode_facts(
    const usp_spotify_playback::PlaybackSnapshot &s,uint32_t updated_at) {
  usp_spotify_modes::Facts out{};
  out.updated_at=updated_at;
  out.valid=s.valid&&s.mode_state_known;
  out.active=s.device_active;out.restricted=s.restricted;
  out.shuffle=s.shuffle_state;out.repeat=repeat_value(s.repeat_state);
  out.shuffle_permission={s.shuffle.known,s.shuffle.allowed};
  out.repeat_context={s.repeat_context.known,s.repeat_context.allowed};
  out.repeat_track={s.repeat_track.known,s.repeat_track.allowed};
  return out;
}
inline usp_spotify_queue::Facts queue_facts(
    const usp_spotify_playback::PlaybackSnapshot &s,uint32_t updated_at) {
  return {updated_at,s.valid,s.device_active,s.restricted};
}
}  // namespace usp_spotify_stage4_binding
