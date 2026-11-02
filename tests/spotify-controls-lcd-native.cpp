#include "usp_spotify_lcd.h"
#include <cassert>
#include <iostream>
using namespace usp_spotify_controls;
/** Main render must enable only the permitted button for the observed playback state. */
int main() {
  lv_obj_t objects[19];
  const usp_spotify_lcd::Widgets w{
    &objects[0],&objects[1],&objects[2],&objects[3],&objects[4],&objects[5],
    &objects[6],&objects[7],&objects[8],&objects[9],&objects[10],&objects[11],
    &objects[12],&objects[13],&objects[14],&objects[15],&objects[16],&objects[17],&objects[18]};
  usp_spotify_presentation::View view{};
  view.mode=usp_spotify_presentation::Mode::PLAYING;view.playing=true;
  ControlView state{15,Phase::IDLE,Outcome::NONE};
  usp_spotify_lcd::render(w,view,2,false,state);
  assert(!w.previous->disabled && !w.play->disabled && !w.next->disabled);
  assert(w.device->disabled && w.progress->disabled && w.volume->disabled);
  assert(w.connection_hint->text=="Control the active Spotify device");
  text_writes=color_writes=slider_writes=state_writes=0;
  for(int i=0;i<60;++i)usp_spotify_lcd::render(w,view,2,false,state);
  assert(text_writes==0 && color_writes==0 && slider_writes==0 && state_writes==0);
  state.allowed_mask=1;usp_spotify_lcd::render(w,view,2,false,state);
  assert(w.play->disabled && w.previous->disabled && w.next->disabled);
  view.mode=usp_spotify_presentation::Mode::PAUSED;view.playing=false;
  usp_spotify_lcd::render(w,view,2,false,state);assert(!w.play->disabled);
  state.phase=Phase::QUEUED;usp_spotify_lcd::render(w,view,2,false,state);
  assert(w.play->disabled && w.connection_hint->text=="Sending command…");
  state.phase=Phase::IDLE;state.allowed_mask=0;state.outcome=Outcome::UNCERTAIN;
  usp_spotify_lcd::render(w,view,2,false,state);
  assert(w.play->disabled && w.connection_hint->text=="Reply unconfirmed · check playback state");
  state={15,Phase::IDLE,Outcome::NONE};view.mode=usp_spotify_presentation::Mode::REAUTH;
  usp_spotify_lcd::render(w,view,2,false,state);
  assert(w.play->disabled && w.previous->disabled && w.next->disabled);
  for(unsigned lang=0;lang<3;++lang) {
    for(const auto result:{Outcome::ACCEPTED,Outcome::UNCERTAIN,Outcome::EXPIRED,
        Outcome::NOT_SENT,Outcome::FORBIDDEN,Outcome::UNAVAILABLE,Outcome::RATE_LIMITED}) {
      state={0,Phase::IDLE,result};assert(hint(state,lang,"fallback")!=std::string("fallback"));
    }
  }
  std::cout<<"SPOTIFY_CONTROLS_LCD_NATIVE=PASS actual_helper permissions playing paused queued uncertainty languages3 change_only60 no_network\n";
}
