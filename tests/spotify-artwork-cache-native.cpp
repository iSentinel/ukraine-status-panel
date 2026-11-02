#include "artwork_cache.h"
#include "artwork_transport.h"
#include <cassert>
#include <functional>
#include <cstdio>
using Cache=usp_spotify_artwork_cache::Cache;
using Picture=usp_spotify_artwork_cache::Picture;
template<class Consumer> void show(Cache &cache,Consumer consumer) {
  assert(cache.present([&](const Picture *picture){consumer(picture);return true;}));
}
constexpr const char *A="https://i.scdn.co/image/albumA";
constexpr const char *B="https://i.scdn.co/image/albumB";
struct Clock { uint32_t time{100};uint32_t now() const {return time;} };
struct Transport {
  unsigned calls{0};int status{200};bool complete{true};std::function<void()> during;
  esphome::usp_spotify_auth::ArtworkResult perform(
      const usp_spotify_artwork::Request &r,uint8_t *output,size_t capacity,
      bool (*current)(void *),void *owner) {
    assert(r.ticket && capacity==131072 && current(owner));++calls;
    output[0]=uint8_t(calls);if(during)during();
    return {status,1,true,complete,!current(owner),{}};
  }
};
struct Decoder {
  unsigned calls{0};bool success{true};std::function<void()> during;
  usp_spotify_artwork_decode::Result decode(const uint8_t *input,size_t size,
      void *workspace,uint16_t *pixels,size_t count,
      usp_spotify_artwork_decode::Image &image,bool (*current)(void *),void *owner) {
    assert(size==1 && workspace && count==90000 && current(owner));++calls;
    for(size_t i=0;i<count;++i)pixels[i]=input[0];
    if(during)during();
    image={300,300};return success ? usp_spotify_artwork_decode::Result::OK
        : usp_spotify_artwork_decode::Result::INVALID;
  }
};
void lifecycle() {
  Clock clock;Transport transport;Decoder decoder;Cache cache;
  unsigned shown=0,hidden=0;const uint16_t *old=nullptr;
  auto main=[&](const Picture *picture){
    if(!picture){++hidden;old=nullptr;return;}
    if(old)assert(old[0]==1);
    old=picture->pixels;++shown;assert(old[0]==shown);
  };
  cache.select(1,A);cache.step(transport,decoder,clock);show(cache,main);
  assert(shown==1 && transport.calls==1);
  cache.step(transport,decoder,clock);show(cache,main);assert(shown==1 && transport.calls==1);
  cache.select(1,B);cache.step(transport,decoder,clock);show(cache,main);assert(shown==2);
  cache.select(2,"");show(cache,main);assert(hidden==1 && !old);
  cache.select(2,A);cache.step(transport,decoder,clock);
  show(cache,[&](const Picture *p){assert(p && p->pixels[0]==3);});
}
void stale_work() {
  Clock clock;Transport transport;Decoder decoder;Cache cache;unsigned published=0;
  cache.select(1,A);
  transport.during=[&](){cache.select(1,B);cache.select(1,A);};
  cache.step(transport,decoder,clock);assert(decoder.calls==0);
  show(cache,[&](const Picture *){++published;});assert(!published);
  transport.during={};cache.step(transport,decoder,clock);
  show(cache,[&](const Picture *p){assert(p && p->pixels[0]==2);++published;});assert(published==1);
  cache.select(1,B);decoder.during=[&](){cache.select(2,"");};
  cache.step(transport,decoder,clock);
  show(cache,[&](const Picture *p){assert(!p);++published;});assert(published==2);
  decoder.during={};cache.select(2,A);cache.step(transport,decoder,clock);
  show(cache,[&](const Picture *p){assert(p && p->pixels[0]==4);});
}
void failures() {
  unsigned publications=0;
  auto main=[&](const Picture *p){assert(p);++publications;};
  {
    Clock clock;Transport transport;Decoder decoder;Cache allocation;
    fixture::fail=true;allocation.select(1,A);allocation.step(transport,decoder,clock);
    assert(!transport.calls);
    fixture::fail=false;clock.time+=9999;allocation.step(transport,decoder,clock);
    assert(!transport.calls);
    clock.time+=1;allocation.step(transport,decoder,clock);show(allocation,main);
    assert(publications==1 && transport.calls==1);
  }
  {
    Cache network;Transport retry;Decoder retry_decoder;Clock retry_clock;
    retry.complete=false;network.select(1,A);network.step(retry,retry_decoder,retry_clock);
    assert(retry.calls==1);
    retry_clock.time+=4999;network.step(retry,retry_decoder,retry_clock);assert(retry.calls==1);
    retry_clock.time+=1;network.step(retry,retry_decoder,retry_clock);assert(retry.calls==2);
    retry_clock.time+=9999;network.step(retry,retry_decoder,retry_clock);assert(retry.calls==2);
    retry_clock.time+=1;retry.complete=true;network.step(retry,retry_decoder,retry_clock);
    show(network,main);assert(publications==2 && retry.calls==3);
  }
}
void permanent_failures() {
  Clock clock;Transport transport;Decoder decoder;Cache cache;
  transport.status=404;transport.complete=false;cache.select(1,A);
  cache.step(transport,decoder,clock);assert(transport.calls==1);
  clock.time+=600000;cache.step(transport,decoder,clock);assert(transport.calls==1);
  transport.status=200;transport.complete=true;cache.select(1,B);cache.step(transport,decoder,clock);
  assert(transport.calls==2);
  decoder.success=false;cache.select(1,A);cache.step(transport,decoder,clock);
  assert(transport.calls==3 && decoder.calls==2);
  clock.time+=600000;cache.step(transport,decoder,clock);assert(transport.calls==3);
  decoder.success=true;cache.select(1,B);cache.step(transport,decoder,clock);
  assert(transport.calls==4);
}
void shutdown() {
  Clock clock;Transport transport;Decoder decoder;Cache cache;unsigned shown=0,hidden=0;
  auto main=[&](const Picture *p){if(p)++shown;else ++hidden;};
  cache.select(1,A);cache.step(transport,decoder,clock);show(cache,main);
  cache.select(1,B);transport.during=[&](){cache.stop();};
  cache.step(transport,decoder,clock);show(cache,main);
  assert(shown==1 && hidden==1 && decoder.calls==1);
  cache.step(transport,decoder,clock);assert(transport.calls==2);
}
void retry_consumer() {
  Clock clock;Transport transport;Decoder decoder;Cache cache;const uint16_t *old=nullptr;
  cache.select(1,A);cache.step(transport,decoder,clock);
  show(cache,[&](const Picture *p){assert(p);old=p->pixels;});
  cache.select(1,B);cache.step(transport,decoder,clock);
  assert(!cache.present([&](const Picture *p){assert(p && old[0]==1);return false;}));
  cache.select(2,"");
  assert(!cache.present([&](const Picture *p){assert(!p && old[0]==1);return false;}));
  show(cache,[&](const Picture *p){assert(!p && old[0]==1);});
  cache.select(2,A);cache.step(transport,decoder,clock);
  show(cache,[&](const Picture *p){assert(p && p->pixels[0]==3);});
}
int main(){retry_consumer();shutdown();lifecycle();stale_work();failures();permanent_failures();puts("ARTWORK_CACHE_NATIVE=PASS stable_loans exponential_retry permanent_404 permanent_decode logout no_device no_network");}
