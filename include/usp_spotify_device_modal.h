#pragma once
#include "esphome/components/usp_spotify_auth/device_catalog.h"
#include "esphome/components/usp_spotify_auth/device_transfer_plan.h"
#include <usp_lcd_navigation_controller.h>
#include <array>
#include <cstdint>
#include <cstdio>

namespace usp_spotify_device_modal {

struct State {
  uint32_t revision{0};
  uint32_t updated_at{0};
  uint8_t count{0};
  uint8_t selectable_mask{0};
  bool open{false};
  bool valid{false};

  void begin() {
    revision=0;updated_at=0;count=0;selectable_mask=0;open=true;valid=false;
  }
  void close() { revision=0;updated_at=0;count=0;selectable_mask=0;open=false;valid=false; }
};
static_assert(sizeof(State)<=16,"Device modal state must remain tiny");

/** Runtime-only modal state; avoids ESPHome globals codegen ordering/persistence. */
inline State &state() {
  static State value{};
  return value;
}

struct Widgets {
  lv_obj_t *root,*title,*status,*refresh,*close;
  std::array<lv_obj_t *,usp_spotify_devices::MAX_DEVICES> rows;
};

inline const char *pick(unsigned language,const char *ua,const char *ru,const char *en) {
  return language==1?ru:(language==2?en:ua);
}

inline bool catalog_fresh(const usp_spotify_devices::Catalog &catalog,
                          uint32_t revision,uint32_t updated_at,uint32_t now) {
  return catalog.valid && revision!=0 && updated_at!=0 &&
      uint32_t(now-updated_at)<usp_spotify_devices::CATALOG_TTL_MS;
}

inline bool any_row_pressed(const Widgets &widgets) {
  for(auto *row:widgets.rows)
    if(row && lv_obj_has_state(row,LV_STATE_PRESSED))return true;
  return false;
}

inline void enabled(lv_obj_t *object,bool value) {
  if(!object)return;
  const bool disabled=lv_obj_has_state(object,LV_STATE_DISABLED);
  if(value&&disabled)lv_obj_remove_state(object,LV_STATE_DISABLED);
  if(!value&&!disabled)lv_obj_add_state(object,LV_STATE_DISABLED);
}

inline void hidden(lv_obj_t *object,bool value) {
  if(!object)return;
  const bool current=lv_obj_has_flag(object,LV_OBJ_FLAG_HIDDEN);
  if(value&&!current)lv_obj_add_flag(object,LV_OBJ_FLAG_HIDDEN);
  if(!value&&current)lv_obj_remove_flag(object,LV_OBJ_FLAG_HIDDEN);
}

inline void clear_rows(const Widgets &widgets) {
  for(auto *row:widgets.rows) {
    if(!row)continue;
    enabled(row,false);hidden(row,true);
  }
}

/** Render only complete fresh catalogs; never replace a row while it is pressed. */
inline bool present(State *state,const Widgets &widgets,
                    const usp_spotify_devices::Catalog &catalog,
                    uint32_t revision,uint32_t updated_at,uint32_t now,
                    unsigned language) {
  if(!state||!state->open)return false;
  if(any_row_pressed(widgets))return false;
  usp_lcd_navigation::label_text(widgets.title,
      pick(language,"Пристрої Spotify","Устройства Spotify","Spotify devices"));
  usp_lcd_navigation::button_text(widgets.refresh,
      pick(language,"ОНОВИТИ","ОБНОВИТЬ","REFRESH"));
  usp_lcd_navigation::button_text(widgets.close,
      pick(language,"ЗАКРИТИ","ЗАКРЫТЬ","CLOSE"));
  if(!catalog_fresh(catalog,revision,updated_at,now)) {
    state->revision=0;state->updated_at=0;state->count=0;state->selectable_mask=0;state->valid=false;
    clear_rows(widgets);
    usp_lcd_navigation::label_text(widgets.status,
        pick(language,"Оновіть список пристроїв","Обновите список устройств","Refresh the device list"));
    return true;
  }
  state->revision=revision;state->updated_at=updated_at;
  state->count=catalog.count;state->selectable_mask=0;state->valid=true;
  for(uint8_t i=0;i<usp_spotify_devices::MAX_DEVICES;++i) {
    auto *row=widgets.rows[i];if(!row)continue;
    if(i>=catalog.count) {enabled(row,false);hidden(row,true);continue;}
    const auto &device=catalog.devices[i];
    const bool selectable=device.selectable&&!device.active&&!device.restricted&&device.id[0];
    if(selectable)state->selectable_mask|=uint8_t(1U<<i);
    char text[usp_spotify_devices::MAX_DEVICE_NAME+usp_spotify_devices::MAX_DEVICE_TYPE+32]{};
    const char *tag=device.active?pick(language,"АКТИВНИЙ","АКТИВНЫЙ","ACTIVE"):
        (selectable?"":pick(language,"НЕДОСТУПНИЙ","НЕДОСТУПЕН","UNAVAILABLE"));
    if(tag[0])std::snprintf(text,sizeof(text),"%s - %s [%s]",device.name.data(),device.type.data(),tag);
    else std::snprintf(text,sizeof(text),"%s - %s",device.name.data(),device.type.data());
    usp_lcd_navigation::button_text(row,text);enabled(row,selectable);hidden(row,false);
  }
  usp_lcd_navigation::label_text(widgets.status,catalog.truncated?
      pick(language,"Показано перші 8 пристроїв","Показаны первые 8 устройств","Showing first 8 devices"):
      pick(language,"Оберіть пристрій","Выберите устройство","Choose a device"));
  return true;
}

/** A click may transfer only the exact fresh row revision that was presented. */
inline bool transfer_target(const State &state,uint8_t index,uint32_t now,uint32_t *revision) {
  if(!revision||!state.open||!state.valid||!state.revision||index>=state.count||index>=8||
      uint32_t(now-state.updated_at)>=usp_spotify_devices::CATALOG_TTL_MS||
      !(state.selectable_mask&(1U<<index)))return false;
  *revision=state.revision;return true;
}

}  // namespace usp_spotify_device_modal
