#pragma once
#include <string>
#include <cstdint>
struct lv_obj_t {std::string text;uint32_t color=0;int value=0;bool disabled=false;bool pressed=false;};
constexpr int LV_ANIM_OFF=0,LV_STATE_DISABLED=1,LV_STATE_PRESSED=2;
inline int text_writes=0,color_writes=0,slider_writes=0,state_writes=0;
inline int lv_slider_get_value(lv_obj_t *widget) {return widget->value;}
inline void lv_slider_set_value(lv_obj_t *widget,int value,int) {
  widget->value=value;++slider_writes;
}
inline void lv_obj_add_state(lv_obj_t *widget,int) {
  if(!widget->disabled){widget->disabled=true;++state_writes;}
}
inline bool lv_obj_has_state(lv_obj_t *widget,int state) {return state==LV_STATE_PRESSED?widget->pressed:widget->disabled;}
inline void lv_obj_remove_state(lv_obj_t *widget,int) {
  if(widget->disabled){widget->disabled=false;++state_writes;}
}
namespace usp_lcd_navigation {
inline void label_text(lv_obj_t *widget,const char *text) {
  if(widget->text!=text){widget->text=text;++text_writes;}
}
inline void label_color(lv_obj_t *widget,uint32_t color) {
  if(widget->color!=color){widget->color=color;++color_writes;}
}
inline void button_text(lv_obj_t *widget,const char *text) {label_text(widget,text);}
}
