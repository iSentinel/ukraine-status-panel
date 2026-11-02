#include "usp_fullscreen_map_cache.h"
#include "legacy-renderer.h"
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>

lv_display_t display;
lv_obj_t *active_screen=nullptr, *created_image=nullptr;
std::vector<Callback> callbacks;
namespace boundary {unsigned present_logs=0,ready_logs=0,warning_logs=0;}
static bool allocation_fails=false;
static unsigned allocations=0;
static void *allocation=nullptr;
namespace esphome {uint32_t millis() {static uint32_t value=0;return ++value;}}

/** Substitute only the PSRAM boundary and poison all unfinished pixels. */
void *heap_caps_aligned_alloc(size_t alignment,size_t bytes,unsigned flags) {
  ++allocations; assert(flags==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(allocation_fails) return nullptr;
  assert(posix_memalign(&allocation,alignment,bytes)==0);
  std::memset(allocation,0xDE,bytes);
  return allocation;
}

/** Deliver the recorded display callbacks through controlled platform events. */
void event(int code) {
  for(const auto &callback:callbacks) {
    if(callback.code!=code) continue;
    lv_event_t e{code,callback.user};callback.function(&e);
  }
}

/** Keep unfinished images hidden and original masks visible across every yield. */
void finish(usp_fullscreen_map_cache::Cache &cache,
            std::array<lv_obj_t,67> &originals) {
  unsigned steps=0;
  while(cache.cold_pending()) {
    assert(!cache.active());
    assert(created_image->flags&LV_OBJ_FLAG_HIDDEN);
    for(const auto &o:originals) assert(!(o.flags&LV_OBJ_FLAG_HIDDEN));
    event(LV_EVENT_RENDER_START);event(LV_EVENT_REFR_READY);
    assert(boundary::present_logs==0);
    cache.cold_step(4096);
    assert(++steps<1000);
  }
  assert(cache.active() && !(created_image->flags&LV_OBJ_FLAG_HIDDEN));
  for(const auto &o:originals) assert(o.flags&LV_OBJ_FLAG_HIDDEN);
}

/** First-frame publication owns original pointers and uses the latest colors. */
void successful() {
  lv_obj_t screen,parent;parent.parent=&screen;active_screen=&screen;
  std::array<lv_obj_t,67> originals{};
  lv_obj_t *pointers[67];int states[25]{};
  for(unsigned i=0;i<67;++i) pointers[i]=&originals[i];
  auto &cache=usp_fullscreen_map_cache::Cache::instance();
  assert(!cache.refresh(&parent,pointers,68,states,0) && allocations==0);
  assert(!cache.refresh(&parent,pointers,67,states,0));
  assert(allocations==1 && cache.cold_pending() && !cache.active());
  cache.cold_step(4096);
  states[4]=32;states[10]=2;
  assert(!cache.refresh(&parent,pointers,67,states,1));
  std::fill_n(pointers,67,nullptr);  // Caller storage must not be retained.
  finish(cache,originals);
  assert(boundary::ready_logs==1 && boundary::present_logs==0);
  const auto *d=static_cast<const lv_image_dsc_t *>(created_image->source);
  std::vector<uint16_t> expected(d->data_size/2);
  usp_map_cache_legacy::Renderer legacy(expected.data());uint16_t colors[27]{};
  colors[0]=usp_map_cache::rgb565(0x07111F);colors[26]=usp_map_cache::rgb565(0x91A8B2);
  for(unsigned i=0;i<25;++i) colors[i+1]=usp_map_cache::rgb565(
      usp_fullscreen_map_cache::region_color_hex(states[i],1));
  legacy.update(colors);
  assert(std::memcmp(d->data,expected.data(),d->data_size)==0);
  assert(created_image->x==63 && created_image->y==12 && created_image->index==0);
  assert(!(created_image->flags&(LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE)));
  event(LV_EVENT_REFR_READY);assert(boundary::present_logs==0);
  event(LV_EVENT_RENDER_START);event(LV_EVENT_REFR_READY);
  assert(boundary::present_logs==1);
  for(unsigned i=0;i<67;++i) pointers[i]=&originals[i];
  assert(cache.refresh(&parent,pointers,67,states,1) && allocations==1);
}

/** Allocation failure keeps the original non-cache path and does not retry. */
void failed() {
  allocation_fails=true;lv_obj_t parent;
  std::array<lv_obj_t,67> originals{};lv_obj_t *pointers[67];int states[25]{};
  for(unsigned i=0;i<67;++i) pointers[i]=&originals[i];
  auto &cache=usp_fullscreen_map_cache::Cache::instance();
  assert(!cache.refresh(&parent,pointers,67,states,0));
  assert(!cache.active() && !cache.cold_pending() && created_image==nullptr);
  assert(allocations==1 && boundary::warning_logs==1);
  allocation_fails=false;
  assert(!cache.refresh(&parent,pointers,67,states,0) && allocations==1);
  for(unsigned batch=0;batch<9;++batch)
    usp_fullscreen_map_cache::update_fallback(pointers,states,0,batch);
  for(const auto &o:originals) assert(!(o.flags&LV_OBJ_FLAG_HIDDEN));
}

int main(int argc,char **) {
  if(argc>1) failed();else successful();
  std::free(allocation);delete created_image;
  std::cout<<"COLD_CACHE_BOUNDARY=PASS controlled LVGL/PSRAM/events; no physical timing proof\n";
}
