
#include "usp_room_aggregate_history.h"
#include "esphome/core/helpers.h"
#include "esp_heap_caps.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

using esphome::usp_room_aggregate_history::RoomAggregateHistory;
using esphome::usp_room_aggregate_history::Aggregate;
uint32_t esphome::host_millis=1000;

static void reset_host(bool clear_nvs=true){
  if(clear_nvs)esphome::host_preferences.persisted.clear();
  esphome::host_preferences.pending.clear();
  esphome::host_preferences.save_calls=0;esphome::host_preferences.sync_calls=0;
  esphome::host_preferences.fail_save=false;esphome::host_preferences.fail_sync=false;
  host_alloc_fail_after=-1;host_alloc_calls=0;esphome::host_millis=1000;
  assert(host_live_bytes==0);
}
static uint32_t day_key(uint16_t year,uint16_t doy){return RoomAggregateHistory::local_day_key(year,doy);}
static void next_day_fields(size_t day,uint16_t *year,uint16_t *doy){
  if(day<366){*year=2024;*doy=static_cast<uint16_t>(day+1);}else{*year=2025;*doy=static_cast<uint16_t>(day-366+1);}
}
static int32_t average_temp(const Aggregate *p){return p&&p->valid_count? p->temperature_sum_centi/static_cast<int32_t>(p->valid_count):0;}

int main(){
  reset_host();
  {
    RoomAggregateHistory h;h.setup();
    assert(!h.active()&&h.active_psram_bytes()==0&&host_alloc_calls==0);
    for(int i=0;i<100000;++i){assert(!h.record(1700000000U+i*300U,2026,300,12,0,0,false));h.loop();}
    assert(!h.active()&&host_alloc_calls==0&&esphome::host_preferences.save_calls==0&&esphome::host_preferences.sync_calls==0);
  }
  assert(host_live_bytes==0);

  reset_host();
  {
    RoomAggregateHistory h;h.setup();
    const uint32_t e=1800000000U;
    assert(h.record(e,2026,300,12,20.0f,40.0f,true));
    assert(h.active()&&h.active_psram_bytes()==27360&&host_alloc_calls==2);
    assert(h.record(e+60,2026,300,12,22.0f,42.0f,true));
    assert(!h.record(e+30,2026,300,12,99.0f,99.0f,true));
    assert(h.record(e+300,2026,300,12,23.0f,43.0f,true));
    assert(h.flush());
    assert(h.hourly_count()==1&&h.daily_count()==1);
    assert(h.hourly_sample(0)->valid_count==2);
    assert(average_temp(h.hourly_sample(0))==2250);
    assert(esphome::host_preferences.sync_calls==1);
  }
  assert(host_live_bytes==0);

  reset_host();
  size_t writes_after_run=0;
  {
    RoomAggregateHistory h;h.setup();
    uint32_t epoch=1704067200U;
    constexpr size_t DAYS=400, PER_DAY=288;
    for(size_t i=0;i<DAYS*PER_DAY;++i){
      uint16_t year,doy;next_day_fields(i/PER_DAY,&year,&doy);
      const uint8_t hour=static_cast<uint8_t>((i%PER_DAY)/12);
      const bool valid=(i%97)!=1;
      const float temp=18.0f+static_cast<float>(i%90)/10.0f;
      const float hum=35.0f+static_cast<float>(i%500)/10.0f;
      assert(h.record(epoch,year,doy,hour,temp,hum,valid));
      esphome::host_millis+=300000U;h.loop();epoch+=300U;
    }
    assert(h.flush());
    assert(h.hourly_count()==768&&h.daily_count()==372);
    assert(h.save_count()>0&&h.save_count()<DAYS*10U);
    writes_after_run=esphome::host_preferences.save_calls;
    assert(writes_after_run<DAYS*20U);
    assert(esphome::host_preferences.persisted.size()<=45U);
  }
  assert(host_live_bytes==0);
  const auto persisted=esphome::host_preferences.persisted;
  {
    esphome::host_preferences.pending.clear();
    RoomAggregateHistory restored;restored.setup();
    assert(restored.active()&&restored.loaded_from_nvs());
    assert(restored.hourly_count()>=744U);
    assert(restored.daily_count()>=366U);
    assert(restored.active_psram_bytes()==27360);
  }
  assert(host_live_bytes==0);

  esphome::host_preferences.persisted=persisted;
  for(auto &entry:esphome::host_preferences.persisted){if(entry.second.size()==esphome::usp_room_aggregate_history::HOUR_PAGE_BYTES){entry.second[20]^=0x5A;break;}}
  {
    RoomAggregateHistory corrupt;corrupt.setup();
    assert(corrupt.corrupt_pages()>=1U);
    assert(corrupt.active());
    assert(corrupt.hourly_count()<768U);
    assert(corrupt.daily_count()>=366U);
  }
  assert(host_live_bytes==0);

  reset_host();host_alloc_fail_after=0;
  {
    RoomAggregateHistory oom;oom.setup();
    assert(!oom.record(1800000000U,2026,300,12,22,50,true));
    assert(!oom.active()&&!oom.storage_ok()&&oom.active_psram_bytes()==0);
    assert(esphome::host_preferences.save_calls==0);
  }
  assert(host_live_bytes==0);

  reset_host();
  {
    RoomAggregateHistory retry;retry.setup();
    assert(retry.record(1800000000U,2026,300,12,22,50,true));
    assert(retry.record(1800000300U,2026,300,12,23,51,true));
    esphome::host_preferences.fail_sync=true;
    assert(!retry.flush());assert(!retry.storage_ok());
    const uint32_t failed_syncs=esphome::host_preferences.sync_calls;
    esphome::host_millis+=29999U;retry.loop();assert(esphome::host_preferences.sync_calls==failed_syncs);
    esphome::host_preferences.fail_sync=false;esphome::host_millis+=1U;retry.loop();
    assert(retry.storage_ok()&&esphome::host_preferences.sync_calls==failed_syncs+1U);
  }
  assert(host_live_bytes==0);

  assert(day_key(2024,59)+1U==day_key(2024,60));
  assert(day_key(2024,60)+1U==day_key(2024,61));
  assert(day_key(2024,366)+1U==day_key(2025,1));
  assert(day_key(2025,366)==0);

  reset_host();
  {
    RoomAggregateHistory dst;dst.setup();uint32_t epoch=1810000000U;
    for(uint8_t hour=0;hour<24;++hour){if(hour==3)continue;for(int i=0;i<12;++i){assert(dst.record(epoch,2026,80,hour,20,50,true));epoch+=300;}}
    assert(dst.flush());assert(dst.daily_count()==1&&dst.daily_sample(0)->valid_count==276);
  }
  assert(host_live_bytes==0);

  std::cout<<"ROOM_AGGREGATE_NATIVE=PASS dormant100000 lazy_psram27360 newest_wins reboot_restore hourly768 daily372 corrupt_fail_closed oom_no_fallback sync_retry dst23 leap writes="<<writes_after_run<<"\n";
}
