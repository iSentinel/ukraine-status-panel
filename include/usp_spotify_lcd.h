#pragma once
#include "esphome/components/usp_spotify_auth/playback_captions.h"
#include "usp_lcd_navigation_controller.h"
#include "esphome/components/usp_spotify_auth/control_captions.h"
#include "esphome/components/usp_spotify_auth/value_control_policy.h"
namespace usp_spotify_lcd {
/** Main-task widget references; the worker never receives these pointers. */
struct Widgets {
  lv_obj_t *status,*track,*artist,*album,*elapsed,*duration;
  lv_obj_t *art_label,*art_caption,*art_hint,*volume_caption;
  lv_obj_t *device_caption,*device_value,*connection_hint;
  lv_obj_t *previous,*play,*next,*device,*progress,*volume;
};


/** Conservative UI admission; worker policy remains authoritative at dispatch. */
struct ValueCapabilities { bool seek{false}; bool volume{false}; };

inline bool fresh_playback(const usp_spotify_playback_worker::PlaybackState &state,
                           usp_spotify_tokens::Phase phase,uint32_t now) {
  return phase==usp_spotify_tokens::Phase::CONNECTED &&
      state.kind==usp_spotify_playback::ResponseKind::JSON && state.snapshot.valid &&
      uint32_t(now-state.updated_at)<usp_spotify_value_controls::FACTS_TTL_MS;
}

inline ValueCapabilities value_capabilities(
    const usp_spotify_playback_worker::PlaybackState &state,
    usp_spotify_tokens::Phase phase,uint32_t now) {
  ValueCapabilities out{};
  if(!fresh_playback(state,phase,now))return out;
  const auto &snapshot=state.snapshot;
  if(!snapshot.device_active || snapshot.restricted || snapshot.private_session)return out;
  out.volume=snapshot.supports_volume &&
      snapshot.volume_percent!=usp_spotify_playback::VOLUME_UNKNOWN &&
      snapshot.volume_percent<=100;
  out.seek=snapshot.has_item && snapshot.duration_ms>1 &&
      snapshot.seek.known && snapshot.seek.allowed;
  return out;
}

/** GET updates never steal a slider while the user's finger owns it. */
inline void slider_value(lv_obj_t *widget,int value) {
  if(lv_obj_has_state(widget,LV_STATE_PRESSED))return;
  if(lv_slider_get_value(widget)!=value)lv_slider_set_value(widget,value,LV_ANIM_OFF);
}

inline bool volume_target(int value,uint32_t *out) {
  if(!out || value<0 || value>100)return false;
  *out=static_cast<uint32_t>(value);return true;
}

/** Endpoints are rejected, not clamped: 1000 permille must not skip the track. */
inline bool seek_target(const usp_spotify_playback_worker::PlaybackState &state,
                        usp_spotify_tokens::Phase phase,uint32_t now,
                        int permille,uint32_t *out) {
  if(!out || permille<=0 || permille>=1000 ||
      !value_capabilities(state,phase,now).seek)return false;
  const uint64_t scaled=static_cast<uint64_t>(state.snapshot.duration_ms)*
      static_cast<uint32_t>(permille);
  const uint32_t position=static_cast<uint32_t>(scaled/1000ULL);
  if(position==0 || position>=state.snapshot.duration_ms)return false;
  *out=position;return true;
}

/** Common label helpers update text and color only when their values change. */
inline void labels(const Widgets &w,const usp_spotify_presentation::View &view,
                   const usp_spotify_captions::Content &text,bool artwork_ready=false) {
  usp_lcd_navigation::label_text(w.status,text.info.status);
  usp_lcd_navigation::label_color(w.status,text.info.color);
  usp_lcd_navigation::label_text(w.track,text.track);
  usp_lcd_navigation::label_text(w.artist,text.artist);
  usp_lcd_navigation::label_text(w.album,text.context.data());
  usp_lcd_navigation::label_text(w.elapsed,view.elapsed.data());
  usp_lcd_navigation::label_text(w.duration,view.duration.data());
  usp_lcd_navigation::label_text(w.art_label,text.art);
  usp_lcd_navigation::label_text(w.art_caption,artwork_ready?text.art:text.no_art);
  usp_lcd_navigation::label_text(w.art_hint,artwork_ready?"Spotify":text.art_hint);
  usp_lcd_navigation::label_text(w.volume_caption,text.volume_caption);
  usp_lcd_navigation::label_text(w.device_caption,text.device_caption);
  usp_lcd_navigation::label_text(w.device_value,text.device);
  usp_lcd_navigation::label_text(w.connection_hint,text.info.hint);
}

/** Change a button state only when needed, keeping identical frames free of redraws. */
inline void enabled(lv_obj_t *widget,bool allowed) {
  const bool disabled=lv_obj_has_state(widget,LV_STATE_DISABLED);
  if(allowed && disabled)lv_obj_remove_state(widget,LV_STATE_DISABLED);
  else if(!allowed && !disabled)lv_obj_add_state(widget,LV_STATE_DISABLED);
}

/** Only fresh permitted actions are enabled; sliders and device selection stay read-only. */
inline void controls(const Widgets &w,const usp_spotify_presentation::View &view,
                     const usp_spotify_captions::Content &text,
                     const usp_spotify_controls::ControlView &state,
                     const ValueCapabilities &values={},bool devices=false) {
  usp_lcd_navigation::button_text(w.previous,text.previous);
  usp_lcd_navigation::button_text(w.play,text.play);
  usp_lcd_navigation::button_text(w.next,text.next);
  usp_lcd_navigation::button_text(w.device,text.device_caption);
  const int progress=static_cast<int>(view.progress_permille);
  slider_value(w.progress,progress);
  const int volume=view.volume_known?static_cast<int>(view.volume_percent):0;
  slider_value(w.volume,volume);
  const bool playback=view.mode==usp_spotify_presentation::Mode::PLAYING ||
      view.mode==usp_spotify_presentation::Mode::PAUSED;
  const auto mask=playback && state.phase==usp_spotify_controls::Phase::IDLE ?
      state.allowed_mask : 0;
  enabled(w.previous,(mask & 8U)!=0);
  enabled(w.play,(mask & (view.playing?2U:1U))!=0);
  enabled(w.next,(mask & 4U)!=0);
  enabled(w.device,devices);
  enabled(w.progress,playback && values.seek);
  enabled(w.volume,playback && values.volume);
}

/** Call only from the main/LVGL task after obtaining one consistent owned View. */
inline void render(const Widgets &widgets,const usp_spotify_presentation::View &view,
                   unsigned language,bool artwork_ready=false,
                   const usp_spotify_controls::ControlView &state={},
                   const ValueCapabilities &values={},bool devices=false) {
  const auto text=usp_spotify_captions::describe(view,language);
  auto presented=text;
  if(view.mode==usp_spotify_presentation::Mode::PLAYING ||
      view.mode==usp_spotify_presentation::Mode::PAUSED)
    presented.info.hint=usp_spotify_controls::hint(state,language,text.info.hint);
  labels(widgets,view,presented,artwork_ready);
  controls(widgets,view,presented,state,values,devices);
}
}  // namespace usp_spotify_lcd
