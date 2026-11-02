#include "spotify_auth.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esp_http_client.h"
#include "esphome/components/usp_weather/network_gate.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <condition_variable>
#include <cstdio>
using Api=esphome::usp_spotify_auth::SpotifyAuthApi;
using Request=esphome::web_server_idf::AsyncWebServerRequest;
using Picture=usp_spotify_artwork_cache::Picture;
constexpr const char *A="https://i.scdn.co/image/albumA";
constexpr const char *B="https://i.scdn.co/image/albumB";
constexpr const char *C="https://i.scdn.co/image/albumC";
void wait_until(const std::function<bool()> &ready) {
  for(unsigned i=0;i<2000;++i){if(ready())return;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  assert(false && "artwork task fixture deadline");
}
std::string post(Api &api,const char *route,std::string body,int expected) {
  Request r(std::string("/usp/spotify/auth/")+route,HTTP_POST,body.size());
  if(!body.empty())api.handleBody(&r,reinterpret_cast<uint8_t *>(body.data()),body.size(),0,body.size());
  api.handleRequest(&r);assert(r.status==expected);return r.body;
}
std::string playback(const char *url) {
  return std::string(R"({"is_playing":true,"progress_ms":1234,"device":{"is_active":true,"is_private_session":false,"is_restricted":false,"supports_volume":true,"name":"Fixture","type":"Computer","volume_percent":42},"item":{"type":"track","name":"Bound track","duration_ms":180000,"artists":[{"name":"Bound artist"}],"album":{"name":"Bound album","images":[{"url":")")+url+R"("}]}}})";
}
void setup_fixture() {
  http_fixture::reset();esphome::host_time=100;
  esphome::host_alloc_count=0;esphome::host_fail_after=SIZE_MAX;
  esphome::usp_network::host_allow_artwork=true;
  http_fixture::response=R"({"access_token":"fixture_access","token_type":"Bearer","expires_in":3600,"refresh_token":"fixture_refresh"})";
  http_fixture::split_spotify=true;http_fixture::player_status=200;
  http_fixture::player_response=playback(A);
  std::ifstream f("tests/spotify_artwork_fixtures/square.jpg",std::ios::binary);assert(f.good());
  http_fixture::artwork_response={std::istreambuf_iterator<char>(f),{}};
  http_fixture::artwork_headers={{"Content-Type","image/jpeg"}};
}
void grant(Api &api) {
  const auto body=post(api,"begin","client_id=fixtureclient&redirect_uri=https%3A%2F%2Freturn.test%2Fcb",200);
  const auto at=body.find("&state=");assert(at!=std::string::npos);
  post(api,"callback","state="+body.substr(at+7,22)+"&code=fixture-code",202);
}
Picture await_picture(Api &api) {
  bool ready=false;Picture result{};
  wait_until([&](){api.present_artwork([&](const Picture *p){
    if(p){assert(p->width==300 && p->height==300 && p->pixels);result=*p;ready=true;}
    return true;
  });return ready;});return result;
}
void check_cdn_headers() {
  unsigned covers=0;
  for(const auto &r:http_fixture::requests) {
    if(r.url.find("https://i.scdn.co/image/")!=0)continue;
    ++covers;assert(r.method==HTTP_METHOD_GET && r.tls_bundle && r.redirect_disabled);
    assert(!r.headers.count("Authorization") && !r.headers.count("Cookie"));
    assert(r.headers.at("Accept")=="image/jpeg" && r.headers.at("Accept-Encoding")=="identity");
  }
  assert(covers==2);
}
void bounded_url(Api &api) {
  host_tasks::pause_at_boundary();
  usp_spotify_playback_worker::PlaybackState state{};
  state.kind=usp_spotify_playback::ResponseKind::JSON;
  state.snapshot.valid=true;state.snapshot.has_item=true;
  state.snapshot.image_url.fill('x');
  assert(api.publish(api.generation(),state));
  bool hidden=false;
  assert(api.present_artwork([&](const Picture *p){assert(!p);hidden=true;return true;}));
  assert(hidden && http_fixture::artwork_calls==1);host_tasks::resume();
}
void change_album(Api &api,const Picture &first) {
  host_tasks::pause_at_boundary();assert(http_fixture::artwork_calls==1);
  http_fixture::player_response=playback(B);esphome::host_time+=5000;host_tasks::resume();
  const auto second=await_picture(api);assert(second.ticket!=first.ticket);
  host_tasks::pause_at_boundary();assert(http_fixture::artwork_calls==2);
  check_cdn_headers();host_tasks::resume();
}
void cancel_inflight(Api &api) {
  std::mutex mutex;std::condition_variable changed;bool entered=false,released=false;
  host_tasks::pause_at_boundary();
  http_fixture::before_perform=[&](){
    if(std::string(http_fixture::config.url).find("https://i.scdn.co/image/")!=0)return;
    std::unique_lock<std::mutex> lock(mutex);entered=true;changed.notify_all();
    changed.wait(lock,[&](){return released;});
  };
  http_fixture::player_response=playback(C);esphome::host_time+=5000;host_tasks::resume();
  {std::unique_lock<std::mutex> lock(mutex);
   assert(changed.wait_for(lock,std::chrono::seconds(2),[&](){return entered;}));}
  post(api,"cancel","",200);
  bool hidden=false;
  assert(api.present_artwork([&](const Picture *p){assert(!p);hidden=true;return true;}));assert(hidden);
  {std::lock_guard<std::mutex> lock(mutex);released=true;}changed.notify_all();
  wait_until([](){return http_fixture::cleanups>=7;});host_tasks::pause_at_boundary();
  assert(http_fixture::artwork_calls==3);
  api.present_artwork([](const Picture *p){assert(!p);return true;});
  http_fixture::before_perform={};host_tasks::resume();
}
void configure_application(Api &api) {
  api.set_application_client_id("fixtureclient");
  api.set_application_redirect_uri("https://return.test/cb");
  api.set_session_key_id(5);
}
int main() {
  esphome::web_server_base::WebServerBase base;
  esphome::web_server_base::global_web_server_base=&base;setup_fixture();
  {Api api;configure_application(api);api.setup();grant(api);host_tasks::release();
   const auto first=await_picture(api);bounded_url(api);change_album(api,first);cancel_inflight(api);}
  host_tasks::join_all();assert(artwork_heap_fixture::allocations.empty());
  puts("SPOTIFY_ARTWORK_HTTP_BINDING_NATIVE=PASS actual_task TLS_adapter JPEG strict_pool album_switch no_CDN_bearer cancellation_inflight no_network no_device");
}
