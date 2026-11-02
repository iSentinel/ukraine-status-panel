#include "stage4_transport.h"
#include "esp_http_client.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace esphome::usp_spotify_auth;
struct Gate { bool ok{true}; };
static bool transfer_current(void *p,const usp_spotify_devices::TransferRequest&r){return static_cast<Gate*>(p)->ok&&r.ticket==1;}
static bool value_current(void *p,const usp_spotify_value_controls::Request&r){return static_cast<Gate*>(p)->ok&&r.ticket==2;}
static bool mode_current(void *p,const usp_spotify_modes::Request&r){return static_cast<Gate*>(p)->ok&&r.ticket==3;}
static bool queue_current(void *p,const usp_spotify_queue::AddRequest&r){return static_cast<Gate*>(p)->ok&&r.ticket==4;}
int main(){
  IdfStage4Transport t;char body[256]{};Gate gate{};
  http_fixture::reset();http_fixture::split_spotify=true;http_fixture::player_status=200;http_fixture::player_response="{}";
  auto get=t.devices("access_A",body,sizeof(body));assert(get.attempted&&get.status==200&&get.complete);assert(http_fixture::requests[0].url=="https://api.spotify.com/v1/me/player/devices");assert(http_fixture::requests[0].method==HTTP_METHOD_GET);
  http_fixture::reset();http_fixture::split_spotify=true;http_fixture::player_status=200;http_fixture::player_response="{}";get=t.queue("access_A",body,sizeof(body));assert(get.attempted&&get.status==200);assert(http_fixture::requests[0].url=="https://api.spotify.com/v1/me/player/queue");
  usp_spotify_devices::TransferRequest dr{};dr.ticket=1;dr.expires_at=2000;std::strcpy(dr.device_id.data(),"devA");http_fixture::reset();http_fixture::split_spotify=true;http_fixture::control_status=204;auto r=t.transfer(dr,"access_A",transfer_current,&gate);assert(r.attempted&&r.status==204&&r.complete);assert(http_fixture::posted==R"({"device_ids":["devA"]})");assert(http_fixture::posted.find("play")==std::string::npos);
  usp_spotify_value_controls::Request vr{};vr.ticket=2;vr.expires_at=2000;vr.kind=usp_spotify_value_controls::Kind::SEEK;vr.value=12345;http_fixture::reset();http_fixture::split_spotify=true;http_fixture::control_status=204;r=t.value(vr,"access_A",value_current,&gate);assert(r.attempted&&r.complete);assert(http_fixture::requests[0].url=="https://api.spotify.com/v1/me/player/seek?position_ms=12345");
  usp_spotify_modes::Request mr{};mr.ticket=3;mr.expires_at=2000;mr.kind=usp_spotify_modes::Kind::REPEAT;mr.repeat=usp_spotify_modes::Repeat::CONTEXT;http_fixture::reset();http_fixture::split_spotify=true;http_fixture::control_status=204;r=t.mode(mr,"access_A",mode_current,&gate);assert(r.attempted&&r.complete);assert(http_fixture::requests[0].url=="https://api.spotify.com/v1/me/player/repeat?state=context");
  usp_spotify_queue::AddRequest qr{};qr.ticket=4;qr.expires_at=2000;std::strcpy(qr.uri.data(),"spotify:track:ABC123");http_fixture::reset();http_fixture::split_spotify=true;http_fixture::control_status=429;http_fixture::control_headers={{"Retry-After","11"}};r=t.queue_add(qr,"access_A",queue_current,&gate);assert(r.attempted&&r.status==429&&r.retry_known&&r.retry_seconds==11);assert(http_fixture::requests[0].url=="https://api.spotify.com/v1/me/player/queue?uri=spotify%3Atrack%3AABC123");assert(http_fixture::requests[0].url.find("device_id")==std::string::npos);
  gate.ok=false;r=t.queue_add(qr,"access_A",queue_current,&gate);assert(!r.attempted&&http_fixture::control_calls==1);
  for(const auto &req:http_fixture::requests){assert(req.tls_bundle&&req.redirect_disabled);assert(req.headers.at("Authorization")=="Bearer access_A");}
  std::cout<<"SPOTIFY_STAGE4_TRANSPORT=PASS get_devices get_queue transfer value mode queue_add strict_tls fixed_urls retry_after revalidate no_network\n";
}
