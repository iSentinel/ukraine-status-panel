#include "stage4_coordinator.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace usp_spotify_stage4_binding;

static usp_spotify_devices::Catalog catalog() {
  usp_spotify_devices::Catalog c{};c.valid=true;c.count=1;
  std::strcpy(c.devices[0].id.data(),"devA");
  std::strcpy(c.devices[0].name.data(),"Office");
  std::strcpy(c.devices[0].type.data(),"speaker");
  c.devices[0].selectable=true;c.devices[0].active=false;c.devices[0].restricted=false;
  c.devices[0].supports_volume=true;c.devices[0].volume_percent=42;return c;
}
static usp_spotify_value_controls::Facts value_facts(uint32_t now) {
  usp_spotify_value_controls::Facts f{};f.updated_at=now;f.duration_ms=200000;
  f.valid=true;f.active=true;f.restricted=false;f.supports_volume=true;f.has_item=true;
  f.seek={true,true};return f;
}
static usp_spotify_modes::Facts mode_facts(uint32_t now) {
  usp_spotify_modes::Facts f{};f.updated_at=now;f.valid=true;f.active=true;f.restricted=false;
  f.shuffle=false;f.repeat=usp_spotify_modes::Repeat::OFF;
  f.shuffle_permission={true,true};f.repeat_context={true,true};f.repeat_track={true,true};return f;
}
static usp_spotify_queue::Facts queue_facts(uint32_t now) {return {now,true,true,false};}
static void session(usp_spotify_controls::Coordinator &basic,Coordinator &stage4,uint32_t gen,uint32_t now) {
  basic.session(gen,true);basic.facts(gen,{now,0x0F,true,true,false,false});
  stage4.session(gen,"user-read-playback-state user-read-currently-playing user-modify-playback-state");
  stage4.device_catalog(gen,catalog(),now);stage4.transfer_action(gen,true,true);
  stage4.value_facts(gen,value_facts(now));stage4.mode_facts(gen,mode_facts(now));stage4.queue_facts(gen,queue_facts(now));
}
static void global_slot() {
  usp_spotify_controls::Coordinator basic;Coordinator stage4(basic);session(basic,stage4,1,1000);
  assert(!stage4.request_transfer(1,0,stage4.device_revision()-1,1001));
  assert(stage4.request_volume(1,55,1001));
  stage4.session(1,"user-read-playback-state user-read-currently-playing user-modify-playback-state");
  assert(stage4.family()==Family::VALUE);
  assert(!stage4.request_queue_add(1,"spotify:track:ABC123",1001));
  assert(!stage4.request_basic(1,usp_spotify_controls::Command::NEXT,1001));
  usp_spotify_value_controls::Request value{};assert(stage4.take_value(1,1002,&value));assert(stage4.current(1,value,1002));
  assert(stage4.finish(1,value,204,true,true,false,false,0,1003));assert(stage4.family()==Family::NONE);
  assert(stage4.request_basic(1,usp_spotify_controls::Command::NEXT,1004));
  assert(!stage4.request_transfer(1,0,stage4.device_revision(),1004));
}
static void shared_429() {
  usp_spotify_controls::Coordinator basic;Coordinator stage4(basic);session(basic,stage4,2,2000);
  assert(stage4.request_seek(2,10000,2001));usp_spotify_value_controls::Request req{};assert(stage4.take_value(2,2002,&req));
  assert(stage4.finish(2,req,429,true,true,true,false,4,2003));
  assert(!stage4.api_ready(2004));
  assert(!stage4.request_transfer(2,0,stage4.device_revision(),2004));
  assert(!stage4.request_shuffle(2,2004));
  assert(!stage4.request_queue_add(2,"spotify:episode:ABC123",2004));
  assert(!stage4.request_basic(2,usp_spotify_controls::Command::PLAY,2004));
  assert(stage4.api_ready(6003));
  stage4.value_facts(2,value_facts(6003));stage4.mode_facts(2,mode_facts(6003));stage4.queue_facts(2,queue_facts(6003));
  stage4.device_catalog(2,catalog(),6003);stage4.transfer_action(2,true,true);
  basic.facts(2,{6003,0x0F,true,true,false,false});
  assert(stage4.request_shuffle(2,6004));
}
static void generation_cancel_before_dispatch() {
  usp_spotify_controls::Coordinator basic;Coordinator stage4(basic);session(basic,stage4,3,3000);
  assert(stage4.request_queue_add(3,"spotify:track:ABC123",3001));
  basic.session(4,true);stage4.session(4,"user-read-playback-state user-modify-playback-state");
  usp_spotify_queue::AddRequest old{};assert(!stage4.take_queue(3,3002,&old));assert(stage4.family()==Family::NONE);
}
static void generation_during_flight_stale_429() {
  usp_spotify_controls::Coordinator basic;Coordinator stage4(basic);session(basic,stage4,5,5000);
  assert(stage4.request_transfer(5,0,stage4.device_revision(),5001));usp_spotify_devices::TransferRequest request{};assert(stage4.take_transfer(5,5002,&request));assert(stage4.current(5,request,5002));
  basic.session(6,true);stage4.session(6,"user-read-playback-state user-modify-playback-state");
  assert(stage4.family()==Family::DEVICE);
  assert(!stage4.current(5,request,5003));
  assert(!stage4.finish(5,request,429,true,true,true,false,3,5004));
  assert(stage4.family()==Family::NONE);
  assert(!stage4.api_ready(5005));
  basic.facts(6,{8004,0x0F,true,true,false,false});
  stage4.device_catalog(6,catalog(),8004);stage4.transfer_action(6,true,true);
  stage4.value_facts(6,value_facts(8004));stage4.mode_facts(6,mode_facts(8004));stage4.queue_facts(6,queue_facts(8004));
  assert(stage4.api_ready(8004));
  assert(stage4.request_volume(6,60,8005));
}
int main(){global_slot();shared_429();generation_cancel_before_dispatch();generation_during_flight_stale_429();std::cout<<"SPOTIFY_STAGE4_BINDING_COORDINATOR=PASS global_slot shared429 generation_cancel stale429 no_replay no_network\n";}
