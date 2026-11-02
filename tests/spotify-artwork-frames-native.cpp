#include "artwork_frames.h"
#include <cassert>
#include <cstdio>
using namespace usp_spotify_frames;
void stale_writer() {
  Frames f; f.select(1); auto a=f.begin(1); assert(a!=NONE);
  f.select(2); auto b=f.begin(2); assert(b!=NONE && b!=a);
  assert(!f.acknowledge(a,1));
  f.select(3); assert(f.begin(3)==NONE);
  assert(!f.finish(a,2,300,300));
  assert(f.finish(a,1,300,300));
  assert(f.view(a).phase==Phase::FREE);
  assert(f.finish(b,2,300,300));
  assert(f.present().visible==NONE);
  assert(f.begin(3)!=NONE);
}
void display_lifetime() {
  Frames f; f.select(1); auto a=f.begin(1);
  assert(f.finish(a,1,300,200)); auto first=f.present();
  assert(first.visible==a && first.retired==NONE && f.visible_current());
  assert(f.begin(1)==NONE && !f.acknowledge(a,1));
  f.select(2); assert(!f.visible_current());
  auto b=f.begin(2); assert(b!=a);
  assert(f.finish(b,2,150,300)); auto next=f.present();
  assert(next.visible==b && next.retired==a);
  f.select(3); assert(f.begin(3)==NONE);
  assert(!f.acknowledge(a,2));
  assert(f.acknowledge(a,1));
  assert(!f.acknowledge(a,1));
  auto c=f.begin(3); assert(c==a);
  assert(!f.acknowledge(a,1));
  assert(f.finish(c,3,1,1));
}
void logout_and_invalid() {
  Frames f; f.select(5); auto a=f.begin(5);
  assert(f.finish(a,5,300,300)); f.present();
  f.select(0); assert(!f.visible_current() && f.begin(0)==NONE);
  assert(f.view(a).phase==Phase::DISPLAYED);
  assert(f.detach()==a && f.view(a).phase==Phase::RETIRED);
  assert(f.acknowledge(a,5));
  assert(!f.finish(NONE,5,300,300) && !f.acknowledge(NONE,5));
  f.select(6); a=f.begin(6);
  assert(f.finish(a,6,301,1)); assert(f.present().visible==NONE);
}
void cycle() {
  Frames f;
  for (uint64_t ticket=1;ticket<=10000;++ticket) {
    f.select(ticket); auto index=f.begin(ticket); assert(index!=NONE);
    assert(f.finish(index,ticket,300,300));
    auto change=f.present(); assert(change.visible==index);
    if (change.retired!=NONE)
      assert(f.acknowledge(change.retired,ticket-1));
    unsigned visible=0;
    for (uint8_t i=0;i<2;++i) visible+=f.view(i).phase==Phase::DISPLAYED;
    assert(visible==1);
  }
}
int main() {
  stale_writer();display_lifetime();logout_and_invalid();cycle();
  puts("ARTWORK_FRAME_OWNERSHIP_NATIVE=PASS stale_writer display_retired_ack logout cycles10000 no_LVGL no_network");
}
