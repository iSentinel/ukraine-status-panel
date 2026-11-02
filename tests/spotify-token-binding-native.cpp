#include "spotify_auth.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esp_http_client.h"
#include <cassert>
#include <iostream>
#include <functional>
#include <condition_variable>
using ProductionApi=esphome::usp_spotify_auth::SpotifyAuthApi;
using Phase=usp_spotify_tokens::Phase;
using Request=esphome::web_server_idf::AsyncWebServerRequest;
constexpr const char *GOOD=R"({"access_token":"access_A","token_type":"Bearer","expires_in":3600,"refresh_token":"old+refresh"})";
constexpr const char *NEXT=R"({"access_token":"access_B","token_type":"Bearer","expires_in":3600})";
class Api : public ProductionApi {
 public:
  std::atomic<int> connections{0};
  /** Host-only contention control; production lock semantics stay unchanged. */
  void with_state_lock(const std::function<void()> &probe) {
    std::lock_guard<std::mutex> lock(this->state_mutex_);probe();
  }
  bool publish(uint32_t generation,Phase phase) override {
    const bool accepted=ProductionApi::publish(generation,phase);
    if(accepted && phase==Phase::CONNECTED)++connections;
    return accepted;
  }
};
void configure_application(Api &api) {
  api.set_application_client_id("client123");
  api.set_application_redirect_uri("https://return.test/cb");
  api.set_session_key_id(5);
}
std::string post(Api &api,const char *route,std::string body,int expected) {
  Request request(std::string("/usp/spotify/auth/")+route,HTTP_POST,body.size());
  if(!body.empty())api.handleBody(&request,reinterpret_cast<uint8_t *>(body.data()),body.size(),0,body.size());
  api.handleRequest(&request);
  if(request.status!=expected)std::cerr<<"POST route="<<route<<" expected="<<expected<<" actual="<<request.status<<"\n";
  assert(request.status==expected);return request.body;
}
/** Deterministic checkpoint POST: one request while the worker is between steps. */
std::string post_at_boundary(Api &api,const char *route,std::string body,int expected) {
  host_tasks::pause_at_boundary();
  const auto result=post(api,route,std::move(body),expected);
  host_tasks::resume();
  return result;
}

std::string status(Api &api) {
  Request request("/usp/spotify/auth/status",HTTP_GET);
  api.handleRequest(&request);
  if(request.status==503)return "";
  assert(request.status==200);return request.body;
}
void grant(Api &api) {
  auto begun=post(api,"begin","client_id=client123&redirect_uri=https%3A%2F%2Freturn.test%2Fcb",200);
  const auto at=begun.find("&state=");assert(at!=std::string::npos);
  post(api,"callback","state="+begun.substr(at+7,22)+"&code=fixture-code",202);
}
void wait_until(const std::function<bool()> &condition) {
  for(int i=0;i<2000;++i) {
    if(condition())return;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  assert(false && "worker fixture deadline");
}
/** Retry only the read-only status GET; a busy response is not a state. */
std::string await_status(Api &api,const std::string &expected) {
  std::string body;
  wait_until([&](){body=status(api);return body.find(expected)!=std::string::npos;});
  return body;
}
void reset() {
  http_fixture::reset();esphome::host_time=100;
  http_fixture::response=GOOD;
  http_fixture::split_spotify=true;
  http_fixture::player_status=200;
  http_fixture::player_response=R"({"is_playing":true,"progress_ms":1234,"device":{"is_active":true,"is_private_session":false,"is_restricted":false,"supports_volume":true,"name":"Fixture","type":"Computer","volume_percent":42},"item":{"type":"track","name":"Bound track","duration_ms":180000,"artists":[{"name":"Bound artist"}],"album":{"name":"Bound album","images":[]}}})";
  esphome::host_alloc_count=0;esphome::host_fail_after=SIZE_MAX;
}

/** Real task entry, HTTP adapter, engine and transport complete code/refresh once. */
void lifecycle() {
  reset();
  {
    Api api;configure_application(api);api.setup();grant(api);host_tasks::release();
    wait_until([&](){return api.connections.load()==1;});
    auto body=await_status(api,"connected");assert(body.find("connected")!=std::string::npos);
    assert(body.find("access_A")==std::string::npos && body.find("old+refresh")==std::string::npos);
    assert(http_fixture::token_calls.load()==1);
    wait_until([](){return http_fixture::player_calls.load()>=1;});
    usp_spotify_playback_worker::PlaybackState playback{};
    wait_until([&](){return api.copy_playback(&playback) && playback.kind==usp_spotify_playback::ResponseKind::JSON;});
    assert(playback.snapshot.valid && std::string(playback.snapshot.item_name.data())=="Bound track");
    post_at_boundary(api,"begin","client_id=client123&redirect_uri=https%3A%2F%2Freturn.test%2Fcb",409);
    http_fixture::response=NEXT;esphome::host_time=3540100;
    wait_until([&](){return api.connections.load()==2;});
    assert(http_fixture::token_calls.load()==2);
    assert(http_fixture::posted.find("refresh_token=old%2Brefresh")!=std::string::npos);
    http_fixture::response=R"({"error":"invalid_grant"})";
    http_fixture::status=400;esphome::host_time=7080100;
    wait_until([&](){return status(api).find("reauth")!=std::string::npos;});
    assert(http_fixture::token_calls.load()==3);
    esphome::host_time+=600000;
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    assert(http_fixture::token_calls.load()==3);
    post_at_boundary(api,"cancel","",200);assert(await_status(api,"idle").find("idle")!=std::string::npos);
  }
  host_tasks::join_all();
}

/** HTTP cancellation proceeds while HTTPS is blocked and rejects the late success. */
void cancellation() {
  reset();
  std::mutex mutex;std::condition_variable changed;bool entered=false,released=false;
  http_fixture::before_perform=[&](){
    std::unique_lock<std::mutex> lock(mutex);entered=true;changed.notify_all();
    changed.wait(lock,[&](){return released;});
  };
  {
    Api api;configure_application(api);api.setup();grant(api);host_tasks::release();
    {std::unique_lock<std::mutex> lock(mutex);
     assert(changed.wait_for(lock,std::chrono::seconds(2),[&](){return entered;}));}
    post(api,"cancel","",200);
    assert(status(api).find("idle")!=std::string::npos);
    {std::lock_guard<std::mutex> lock(mutex);released=true;}changed.notify_all();
    wait_until([](){return http_fixture::cleanups.load()==1;});
    host_tasks::pause_at_boundary();
    assert(api.connections.load()==0);
    assert(await_status(api,"idle").find("idle")!=std::string::npos);
    assert(api.connections.load()==0);
    assert(http_fixture::token_calls.load()==1);
    host_tasks::resume();
  }
  host_tasks::join_all();
}

/** A Player API 401 destroys the rejected RAM token and returns to reauthorization. */
void player_reauth() {
  reset();
  {
    Api api;configure_application(api);api.setup();grant(api);host_tasks::release();
    wait_until([](){return http_fixture::player_calls.load()>=1;});
    // A request counter marks its start. Change time only after its step ends.
    host_tasks::pause_at_boundary();
    assert(http_fixture::cleanups.load()==2);
    http_fixture::player_status=401;http_fixture::player_response.clear();
    esphome::host_time+=5000;
    host_tasks::resume();
    wait_until([&](){return status(api).find("reauth")!=std::string::npos;});
    const auto calls=http_fixture::player_calls.load();
    esphome::host_time+=60000;
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    assert(http_fixture::player_calls.load()==calls);
  }
  host_tasks::join_all();
}

/** A failed task or PSRAM context leaves the protected setup page usable but unavailable. */
void unavailable() {
  for(int mode=0;mode<2;++mode) {
    reset();host_tasks::fail_create=mode==0;
    esphome::host_fail_after=mode==1?1:SIZE_MAX;
    {
      Api api;configure_application(api);api.setup();assert(!api.is_failed());
      assert(status(api).find("\"exchange_available\":false")!=std::string::npos);
      post(api,"begin","client_id=client123&redirect_uri=https%3A%2F%2Freturn.test%2Fcb",503);
      Request page("/usp/spotify/auth/",HTTP_GET);api.handleRequest(&page);assert(page.status==200);
      assert(http_fixture::calls.load()==0);
    }
    host_tasks::fail_create=false;host_tasks::join_all();
  }
}
/** Force the real status handler to return busy, then recover by GET only. */
void busy_status() {
  reset();
  {
    Api api;configure_application(api);api.setup();
    std::mutex mutex;std::condition_variable changed;bool held=false,released=false;
    std::thread holder([&](){api.with_state_lock([&](){
      std::unique_lock<std::mutex> lock(mutex);held=true;changed.notify_all();
      changed.wait(lock,[&](){return released;});
    });});
    {std::unique_lock<std::mutex> lock(mutex);
     assert(changed.wait_for(lock,std::chrono::seconds(2),[&](){return held;}));}
    assert(status(api).empty());
    {std::lock_guard<std::mutex> lock(mutex);released=true;}changed.notify_all();
    holder.join();host_tasks::release();
    assert(await_status(api,"idle").find("idle")!=std::string::npos);
    assert(api.connections.load()==0 && http_fixture::calls.load()==0);
  }
  host_tasks::join_all();
}

int main() {
  esphome::web_server_base::WebServerBase base;
  esphome::web_server_base::global_web_server_base=&base;
  std::cerr<<"CASE lifecycle\n";lifecycle();
  std::cerr<<"CASE cancellation\n";cancellation();
  std::cerr<<"CASE player_reauth\n";player_reauth();
  std::cerr<<"CASE unavailable\n";unavailable();
  std::cerr<<"CASE busy_status\n";busy_status();
  std::cout<<"SPOTIFY_TOKEN_BINDING_PREFLIGHT=PASS actual_task_adapter_engine_transport playback_GET_snapshot_player401 refresh invalid_grant cancellation_inflight startup_fail no_network\n";
}
