/** Exercise cancellation with real LVGL and the physical-pointer timing fixture. */
#define main existing_navigation_tests_main
#include "navigation.cpp"
#undef main

/** A communication failure must not activate or toggle a pressed control. */
void fault_on_button(bool fullscreen, bool delayed_release) {
  Fixture fixture(false, true);
  fixture.controller.set_fullscreen(fullscreen);
  auto *button=lv_button_create(lv_display_get_screen_active(fixture.display));
  lv_obj_set_pos(button,600,200); lv_obj_set_size(button,160,80);
  lv_obj_add_flag(button,LV_OBJ_FLAG_CHECKABLE);
  int clicks=0;
  lv_obj_add_event_cb(button,[](lv_event_t *e) {
    ++*static_cast<int *>(lv_event_get_user_data(e));
  },LV_EVENT_CLICKED,&clicks);
  fixture.physical->feed(650,240,true,80);
  fixture.controller.cancel_touch_fault();
  if(delayed_release) fixture.physical->advance(2500);
  fixture.physical->feed(650,240,false,80);
  assert(clicks==0 && !lv_obj_has_state(button,LV_STATE_CHECKED));
  assert(fixture.controller.take_action()==usp_lcd_gesture::Action::NONE);
  fixture.physical->feed(650,240,true,80);
  fixture.physical->feed(650,240,false,80);
  assert(clicks==1 && lv_obj_has_state(button,LV_STATE_CHECKED));
  assert(fixture.controller.take_action()==usp_lcd_gesture::Action::NONE);
}

/** A failed hold must not open the navigation menu later. */
void fault_on_header() {
  Fixture fixture(false,true);
  fixture.physical->feed(150,30,true,1000);
  fixture.controller.cancel_touch_fault();
  fixture.physical->advance(2500);
  fixture.physical->feed(150,30,false,80);
  assert(fixture.controller.take_action()==usp_lcd_gesture::Action::NONE);
  assert(lv_obj_has_flag(fixture.menu,LV_OBJ_FLAG_HIDDEN));
}

/** Queued navigation must be discarded when its touch becomes untrusted. */
void fault_during_swipe() {
  Fixture fixture(false,true);
  fixture.physical->feed(700,420,true,80);
  fixture.physical->feed(350,420,true,80);
  fixture.controller.cancel_touch_fault();
  fixture.physical->feed(350,420,false,80);
  assert(fixture.controller.take_action()==usp_lcd_gesture::Action::NONE);
}

int main() {
  lv_init();
  for(bool fullscreen:{false,true})
    for(bool delayed:{false,true}) fault_on_button(fullscreen,delayed);
  fault_on_header(); fault_during_swipe();
  std::puts("CT2_CANCEL_LVGL=PASS buttons2x2 no_click no_toggle recovery_click1 hold_cancel swipe_cancel");
  lv_deinit();
}
