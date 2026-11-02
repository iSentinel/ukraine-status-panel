#pragma once
#include <usp_lcd_navigation_controller.h>
#include "esphome/components/usp_spotify_auth/queue_policy.h"
#include "esphome/components/usp_spotify_auth/playback_worker_engine.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#if defined(USE_ESP32)
#include "esp_heap_caps.h"
#include <new>
#endif

namespace usp_spotify_queue_modal {
constexpr size_t ROWS=usp_spotify_queue::MAX_ITEMS;
struct ViewState {
  std::array<std::array<char,usp_spotify_queue::MAX_URI+1>,ROWS> uri{};
  uint32_t updated_at{0};uint8_t count{0};bool valid{false},truncated{false};
};
struct Runtime {usp_spotify_queue::Queue queue{};ViewState view{};};
struct Widgets {lv_obj_t *root,*title,*status,*current,*footer,*refresh,*close;std::array<lv_obj_t*,ROWS> rows{};};
inline Runtime *runtime(){
#if defined(USE_ESP32)
  static Runtime *value=[](){void*mem=heap_caps_calloc(1,sizeof(Runtime),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);return mem?new(mem) Runtime{}:nullptr;}();
  return value;
#else
  static Runtime value{};return &value;
#endif
}
inline const char *pick(unsigned lang,const char *ua,const char *ru,const char *en){return lang==1?ru:lang==2?en:ua;}
inline void enabled(lv_obj_t *widget,bool allowed){const bool disabled=lv_obj_has_state(widget,LV_STATE_DISABLED);if(allowed&&disabled)lv_obj_remove_state(widget,LV_STATE_DISABLED);else if(!allowed&&!disabled)lv_obj_add_state(widget,LV_STATE_DISABLED);}
inline bool pressed(const Widgets&w){for(auto*row:w.rows)if(lv_obj_has_state(row,LV_STATE_PRESSED))return true;return false;}
inline bool fresh(const usp_spotify_queue::Queue&q,uint32_t updated_at,uint32_t now){return q.valid&&updated_at&&uint32_t(now-updated_at)<usp_spotify_queue::QUEUE_TTL_MS;}
inline bool playback_add_allowed(const usp_spotify_playback_worker::PlaybackState&s,usp_spotify_tokens::Phase phase,uint32_t now){return phase==usp_spotify_tokens::Phase::CONNECTED&&s.kind==usp_spotify_playback::ResponseKind::JSON&&s.snapshot.valid&&s.snapshot.device_active&&!s.snapshot.restricted&&uint32_t(now-s.updated_at)<usp_spotify_queue::QUEUE_TTL_MS;}
inline void clear_view(ViewState*state){if(state)*state={};}
inline bool add_target(const ViewState&state,uint8_t index,uint32_t now,const char**uri){if(uri)*uri=nullptr;if(!uri||!state.valid||index>=state.count||uint32_t(now-state.updated_at)>=usp_spotify_queue::QUEUE_TTL_MS||!usp_spotify_queue::spotify_uri(state.uri[index].data()))return false;*uri=state.uri[index].data();return true;}
inline bool present(ViewState*state,const Widgets&w,const usp_spotify_queue::Queue&q,uint32_t updated_at,uint32_t now,unsigned lang,bool can_add){
  if(!state||pressed(w))return false;
  lang=lang<3?lang:0;
  usp_lcd_navigation::label_text(w.title,pick(lang,"Черга Spotify","Очередь Spotify","Spotify queue"));
  usp_lcd_navigation::button_text(w.refresh,pick(lang,"ОНОВИТИ","ОБНОВИТЬ","REFRESH"));
  usp_lcd_navigation::button_text(w.close,pick(lang,"ЗАКРИТИ","ЗАКРЫТЬ","CLOSE"));
  if(!fresh(q,updated_at,now)){clear_view(state);usp_lcd_navigation::label_text(w.status,pick(lang,"Оновіть чергу","Обновите очередь","Refresh queue"));usp_lcd_navigation::label_text(w.current,pick(lang,"Зараз: —","Сейчас: —","Now: —"));usp_lcd_navigation::label_text(w.footer,"");for(auto*row:w.rows){enabled(row,false);usp_lcd_navigation::hidden(row,true);}return true;}
  ViewState next{};next.valid=true;next.updated_at=updated_at;next.count=q.count;next.truncated=q.truncated;
  char current[420]{};if(q.has_current)std::snprintf(current,sizeof(current),"%s: %s · %s",pick(lang,"Зараз","Сейчас","Now"),q.current.name.data(),q.current.subtitle.data());else std::snprintf(current,sizeof(current),"%s",pick(lang,"Зараз: —","Сейчас: —","Now: —"));usp_lcd_navigation::label_text(w.current,current);
  char status[96]{};std::snprintf(status,sizeof(status),"%u / %u",unsigned(q.count),unsigned(ROWS));usp_lcd_navigation::label_text(w.status,status);
  usp_lcd_navigation::label_text(w.footer,q.truncated?pick(lang,"Показано 12 · є ще","Показано 12 · есть ещё","12 shown · more available"):pick(lang,"Натисніть +, щоб додати копію","Нажмите +, чтобы добавить копию","Tap + to add a copy"));
  for(size_t i=0;i<ROWS;++i){auto*row=w.rows[i];if(i>=q.count){enabled(row,false);usp_lcd_navigation::hidden(row,true);continue;}next.uri[i]=q.items[i].uri;char text[420]{};std::snprintf(text,sizeof(text),"+ %u · %s · %s",unsigned(i+1),q.items[i].name.data(),q.items[i].subtitle.data());usp_lcd_navigation::button_text(row,text);usp_lcd_navigation::hidden(row,false);enabled(row,can_add&&usp_spotify_queue::spotify_uri(next.uri[i].data()));}
  *state=next;return true;
}
} // namespace usp_spotify_queue_modal
