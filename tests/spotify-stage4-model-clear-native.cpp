#include "queue_policy.h"
#include "device_catalog.h"
#include <algorithm>
#include <cassert>
#include <iostream>
template<class Array> bool zero(const Array &value) {
  return std::all_of(value.begin(),value.end(),[](char ch){return ch==0;});
}
void queue_reset() {
  usp_spotify_queue::Queue queue;
  queue.current.name.fill('x');queue.current.type=usp_spotify_queue::ItemType::TRACK;
  queue.has_current=true;queue.count=12;queue.valid=true;queue.truncated=true;
  for(auto &item:queue.items) {
    item.uri.fill('x');item.name.fill('y');item.subtitle.fill('z');
    item.duration_ms=123;item.type=usp_spotify_queue::ItemType::EPISODE;
  }
  queue.clear();
  assert(!queue.has_current&&!queue.count&&!queue.valid&&!queue.truncated);
  assert(zero(queue.current.name)&&queue.current.type==usp_spotify_queue::ItemType::NONE);
  for(const auto &item:queue.items) {
    assert(zero(item.uri)&&zero(item.name)&&zero(item.subtitle));
    assert(item.duration_ms==0&&item.type==usp_spotify_queue::ItemType::NONE);
  }
}
void catalog_reset() {
  usp_spotify_devices::Catalog catalog;
  catalog.count=8;catalog.valid=true;catalog.truncated=true;
  for(auto &device:catalog.devices) {
    device.id.fill('x');device.name.fill('y');device.type.fill('z');
    device.volume_percent=42;device.active=true;device.private_session=true;
    device.restricted=false;device.supports_volume=true;device.selectable=true;
  }
  catalog.clear();
  assert(!catalog.count&&!catalog.valid&&!catalog.truncated);
  for(const auto &device:catalog.devices) {
    assert(zero(device.id)&&zero(device.name)&&zero(device.type));
    assert(device.volume_percent==usp_spotify_devices::VOLUME_UNKNOWN);
    assert(!device.active&&!device.private_session&&device.restricted);
    assert(!device.supports_volume&&!device.selectable);
  }
}
int main() {
  for(unsigned i=0;i<10000;++i){queue_reset();catalog_reset();}
  std::cout<<"SPOTIFY_MODEL_CLEAR_NATIVE=PASS queue12 catalog8 all_fields sentinel_defaults stress10000 no_network\n";
}
