#include "usp_spotify_lcd.h"
#include <cassert>
#include <iostream>
using usp_spotify_presentation::Mode;
int main() {
  lv_obj_t objects[19];
  const usp_spotify_lcd::Widgets w{
    &objects[0],&objects[1],&objects[2],&objects[3],&objects[4],&objects[5],
    &objects[6],&objects[7],&objects[8],&objects[9],&objects[10],&objects[11],
    &objects[12],&objects[13],&objects[14],&objects[15],&objects[16],&objects[17],&objects[18]};
  usp_spotify_presentation::View view{};
  view.mode=Mode::PLAYING;view.playing=true;view.progress_permille=100;
  view.volume_known=true;view.volume_percent=55;
  std::strcpy(view.title.data(),"Track");std::strcpy(view.elapsed.data(),"0:10");
  usp_spotify_lcd::render(w,view,2);
  assert(w.track->text=="Track" && w.progress->value==100 && w.volume->value==55);
  assert(state_writes==6);
  text_writes=color_writes=slider_writes=state_writes=0;
  for(unsigned i=0;i<60;++i)usp_spotify_lcd::render(w,view,2);
  assert(text_writes==0 && color_writes==0 && slider_writes==0 && state_writes==0);
  view.progress_permille=101;usp_spotify_lcd::render(w,view,2);
  assert(slider_writes==1 && text_writes==0);
  view={};view.mode=Mode::DISCONNECTED;
  usp_spotify_lcd::render(w,view,2);
  assert(w.track->text=="Spotify is not connected" && w.progress->value==0 && w.volume->value==0);
  for(auto *control:{w.previous,w.play,w.next,w.device,w.progress,w.volume})assert(control->disabled);
  view.mode=Mode::PLAYING;
  for(unsigned language=0;language<3;++language) {
    const auto text=usp_spotify_captions::describe(view,language);
    usp_spotify_lcd::render(w,view,language,true);
    assert(w.art_caption->text==text.art && w.art_hint->text=="Spotify");
    usp_spotify_lcd::render(w,view,language,false);
    assert(w.art_caption->text==text.no_art && w.art_hint->text==text.art_hint);
  }
  std::cout<<"SPOTIFY_LCD_HELPER_PREFLIGHT=PASS actual_helper fake_LVGL change_only60 controls_disabled stale_values_clear no_provider_device\n";
}
