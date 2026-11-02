#pragma once
#include <cstdint>
#include <vector>
struct lv_obj_t {
  uint32_t flags{0}; lv_obj_t *parent{nullptr}; const void *source{nullptr};
  int x{0}, y{0}, index{-1}; unsigned invalidations{0};
};
struct lv_display_t {};
struct lv_event_t { int code; void *user; };
using lv_event_cb_t = void (*)(lv_event_t *);
struct Callback {lv_event_cb_t function; int code; void *user;};
extern lv_display_t display;
extern lv_obj_t *active_screen;
extern lv_obj_t *created_image;
extern std::vector<Callback> callbacks;
constexpr uint32_t LV_OBJ_FLAG_HIDDEN=1, LV_OBJ_FLAG_CLICKABLE=2, LV_OBJ_FLAG_SCROLLABLE=4;
constexpr int LV_PART_MAIN=0, LV_EVENT_RENDER_START=1, LV_EVENT_REFR_READY=2;
constexpr int LV_IMAGE_HEADER_MAGIC=9, LV_COLOR_FORMAT_RGB565=16;
struct lv_image_dsc_t {
  struct {uint32_t magic{},cf{},w{},h{},stride{};} header;
  uint32_t data_size{}; const uint8_t *data{};
};
inline uint32_t lv_color_hex(uint32_t color) {return color;}
inline void lv_obj_set_style_image_recolor(lv_obj_t *,uint32_t,int) {}
inline void lv_obj_add_flag(lv_obj_t *o,uint32_t flag) {o->flags |= flag;}
inline void lv_obj_clear_flag(lv_obj_t *o,uint32_t flag) {o->flags &= ~flag;}
inline void lv_obj_invalidate(lv_obj_t *o) {++o->invalidations;}
inline void lv_image_cache_drop(const void *) {}
inline lv_obj_t *lv_image_create(lv_obj_t *p) {
  created_image=new lv_obj_t; created_image->parent=p;
  created_image->flags=LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE;
  return created_image;
}
inline void lv_image_set_src(lv_obj_t *o,const void *p) {o->source=p;}
inline void lv_obj_set_pos(lv_obj_t *o,int x,int y) {o->x=x;o->y=y;}
inline void lv_obj_move_to_index(lv_obj_t *o,int index) {o->index=index;}
inline lv_display_t *lv_obj_get_display(lv_obj_t *) {return &display;}
inline void lv_display_add_event_cb(lv_display_t *,lv_event_cb_t fn,int code,void *user) {
  callbacks.push_back({fn,code,user});
}
inline void *lv_event_get_user_data(lv_event_t *e) {return e->user;}
inline int lv_event_get_code(lv_event_t *e) {return e->code;}
inline lv_obj_t *lv_obj_get_screen(lv_obj_t *o) {
  while(o->parent) o=o->parent;
  return o;
}
inline lv_obj_t *lv_screen_active() {return active_screen;}
