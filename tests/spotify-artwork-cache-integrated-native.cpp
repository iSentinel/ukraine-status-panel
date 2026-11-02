#include "artwork_cache.h"
#include "artwork_transport.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
#include <thread>
#include <atomic>
using Cache=usp_spotify_artwork_cache::Cache;
using Picture=usp_spotify_artwork_cache::Picture;
template<class Consumer> void show(Cache &cache,Consumer consumer) {
  assert(cache.present([&](const Picture *picture){consumer(picture);return true;}));
}
constexpr const char *A="https://i.scdn.co/image/albumA";
constexpr const char *B="https://i.scdn.co/image/albumB";
struct Clock { uint32_t now() const { return 100; } };
struct Transport {
  std::vector<uint8_t> input;
  void load(const char *path) {
    std::ifstream f(path,std::ios::binary);assert(f.good());
    input={std::istreambuf_iterator<char>(f),{}};
  }
  esphome::usp_spotify_auth::ArtworkResult perform(
      const usp_spotify_artwork::Request &,uint8_t *output,size_t capacity,
      bool (*current)(void *),void *owner) {
    assert(input.size()<=capacity && current(owner));
    std::copy(input.begin(),input.end(),output);
    return {200,input.size(),true,true,false,{}};
  }
};
struct Decoder {
  Cache *cancel_owner{nullptr};
  struct Probe {Decoder *decoder;bool (*current)(void *);void *owner;unsigned count{0};};
  static bool check(void *value) {
    auto &p=*static_cast<Probe *>(value);
    if(p.decoder->cancel_owner && ++p.count==4)p.decoder->cancel_owner->select(2,"");
    return p.current(p.owner);
  }
  usp_spotify_artwork_decode::Result decode(const uint8_t *input,size_t size,
      void *workspace,uint16_t *pixels,size_t count,
      usp_spotify_artwork_decode::Image &image,bool (*current)(void *),void *owner) {
    Probe probe{this,current,owner};
    return usp_spotify_artwork_decode::decode(input,size,workspace,pixels,count,image,check,&probe);
  }
};
uint64_t checksum(const Picture &picture) {
  uint64_t result=1469598103934665603ULL;
  for(size_t i=0;i<size_t(picture.width)*picture.height;++i)
    result=(result^picture.pixels[i])*1099511628211ULL;
  return result;
}
void swapping_and_acknowledgement() {
  Cache cache;Clock clock;Transport transport;Decoder decoder;Picture first{};
  transport.load("square.jpg");cache.select(1,A);cache.step(transport,decoder,clock);
  show(cache,[&](const Picture *p){assert(p && p->width==300 && p->height==300);first=*p;});
  const auto before=checksum(first);
  transport.load("landscape.jpg");cache.select(1,B);cache.step(transport,decoder,clock);
  assert(checksum(first)==before);
  std::thread writer;std::atomic<bool> started{false};
  show(cache,[&](const Picture *p){
    assert(p && p->index!=first.index && p->width==300 && p->height==150);
    // A new writer cannot reclaim the first loan until this callback returns.
    writer=std::thread([&](){started=true;cache.select(1,A);cache.step(transport,decoder,clock);});
    while(!started.load())std::this_thread::yield();
    assert(checksum(first)==before);
  });
  writer.join();
  show(cache,[&](const Picture *p){assert(p && p->index==first.index && p->pixels==first.pixels);});
}
void cancelled_real_decoder() {
  Cache cache;Clock clock;Transport transport;Decoder decoder;unsigned events=0;
  transport.load("square.jpg");cache.select(1,A);cache.step(transport,decoder,clock);
  show(cache,[&](const Picture *p){assert(p);++events;});
  decoder.cancel_owner=&cache;cache.select(1,B);cache.step(transport,decoder,clock);
  show(cache,[&](const Picture *p){assert(!p);++events;});
  assert(events==2);
  decoder.cancel_owner=nullptr;cache.select(2,A);cache.step(transport,decoder,clock);
  show(cache,[&](const Picture *p){assert(p && p->width==300);++events;});assert(events==3);
}
int main() {
  swapping_and_acknowledgement();cancelled_real_decoder();
  puts("ARTWORK_CACHE_INTEGRATED_NATIVE=PASS actual_decoder strict_pool threaded_ack cancellation no_network no_device");
}
