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
constexpr const char *GOOD=R"({"scope":"user-modify-playback-state","access_token":"access_A","token_type":"Bearer","expires_in":3600,"refresh_token":"old+refresh"})";
constexpr const char *NEXT=R"({"access_token":"access_B","token_type":"Bearer","expires_in":3600})";
class Api : public ProductionApi {
 public:
  using ProductionApi::publish;
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
  http_fixture::player_response=R"({"actions":{"disallows":{}},"is_playing":true,"progress_ms":1234,"device":{"is_active":true,"is_private_session":false,"is_restricted":false,"supports_volume":true,"name":"Fixture","type":"Computer","volume_percent":42},"item":{"type":"track","name":"Bound track","duration_ms":180000,"artists":[{"name":"Bound artist"}],"album":{"name":"Bound album","images":[]}}})";
  esphome::host_alloc_count=0;esphome::host_fail_after=SIZE_MAX;
}


/** Wait for real worker publication, then hold its next iteration for deterministic taps. */
void boot(Api &api) {
  configure_application(api);api.setup();grant(api);host_tasks::release();
  wait_until([&](){
    usp_spotify_controls::ControlView view{};
    return api.copy_controls(&view) && view.allowed_mask==15;
  });
  host_tasks::pause_at_boundary();
}

/** Four user actions reach exactly the fixed endpoints, with no optimistic metadata. */
void four_commands() {
  reset();
  {
    Api api;boot(api);
    const char *paths[]={"pause","play","next","previous"};
    for(int i=0;i<4;++i) {
      const int calls=http_fixture::control_calls.load();
      const bool accepted=i<2?api.request_play_pause():api.request_control(
          i==2?usp_spotify_controls::Command::NEXT:usp_spotify_controls::Command::PREVIOUS);
      if(!accepted) {
        usp_spotify_playback_worker::PlaybackState dbg_state{};
        Api::PlaybackPhase dbg_phase{};
        usp_spotify_controls::ControlView dbg_view{};
        const bool playback_ok=api.copy_playback(&dbg_state,&dbg_phase);
        const bool controls_ok=api.copy_controls(&dbg_view);
        std::cerr<<"TAP_REJECT i="<<i<<" gen="<<api.generation()
                 <<" playback_ok="<<playback_ok<<" phase="<<int(dbg_phase)
                 <<" playing="<<dbg_state.snapshot.is_playing
                 <<" controls_ok="<<controls_ok<<" control_phase="<<int(dbg_view.phase)
                 <<" outcome="<<int(dbg_view.outcome)<<" allowed="<<int(dbg_view.allowed_mask)
                 <<" host_time="<<esphome::host_time<<" at_boundary="<<host_tasks::at_boundary
                 <<" pause_requested="<<host_tasks::pause_requested<<"\n";
      }
      assert(accepted);
      assert(!api.request_control(usp_spotify_controls::Command::NEXT));
      http_fixture::before_control=[&](){
        usp_spotify_playback_worker::PlaybackState state{};
        usp_spotify_controls::ControlView view{};
        assert(api.copy_playback(&state) && api.copy_controls(&view));
        assert(std::string(state.snapshot.item_name.data())=="Bound track");
        assert(view.allowed_mask==0 && view.phase==usp_spotify_controls::Phase::IN_FLIGHT);
        assert(!api.request_play_pause());
      };
      if(i<2) {
        const auto old=i==0?std::string("\"is_playing\":true"):std::string("\"is_playing\":false");
        const auto replacement=i==0?std::string("\"is_playing\":false"):std::string("\"is_playing\":true");
        auto &body=http_fixture::player_response;body.replace(body.find(old),old.size(),replacement);
      }
      host_tasks::resume();
      wait_until([&](){return http_fixture::control_calls.load()==calls+1;});
      host_tasks::pause_at_boundary();
      assert(http_fixture::requests[http_fixture::requests.size()-2].url==
             std::string("https://api.spotify.com/v1/me/player/")+paths[i]);
      assert(http_fixture::requests[http_fixture::requests.size()-2].method==
             (i<2?HTTP_METHOD_PUT:HTTP_METHOD_POST));
      usp_spotify_controls::ControlView view{};assert(api.copy_controls(&view));
      assert(view.allowed_mask==15 && view.outcome==usp_spotify_controls::Outcome::ACCEPTED);
    }
    assert(http_fixture::control_calls.load()==4 && http_fixture::player_calls.load()==5);
    host_tasks::resume();
  }
  host_tasks::join_all();
}

/** An expired queued tap never starts HTTP on a later worker iteration. */
void expired_tap() {
  reset();
  {
    Api api;boot(api);assert(api.request_play_pause());esphome::host_time+=2000;
    host_tasks::resume();std::this_thread::sleep_for(std::chrono::milliseconds(10));
    host_tasks::pause_at_boundary();
    usp_spotify_controls::ControlView view{};assert(api.copy_controls(&view));
    assert(http_fixture::control_calls.load()==0);
    assert(view.outcome==usp_spotify_controls::Outcome::EXPIRED);
    host_tasks::resume();
  }
  host_tasks::join_all();
}

/** A lost response is observed with GET and is never resent automatically. */
void uncertain_command() {
  reset();
  {
    Api api;boot(api);
    http_fixture::before_control=[](){http_fixture::fail_perform=true;};
    assert(api.request_control(usp_spotify_controls::Command::NEXT));
    host_tasks::resume();wait_until([](){return http_fixture::control_calls.load()==1;});
    host_tasks::pause_at_boundary();
    usp_spotify_controls::ControlView view{};assert(api.copy_controls(&view));
    assert(view.outcome==usp_spotify_controls::Outcome::UNCERTAIN && view.allowed_mask==0);
    http_fixture::fail_perform=false;http_fixture::before_control={};
    esphome::host_time+=30000;host_tasks::resume();
    wait_until([&](){return api.copy_controls(&view) && view.allowed_mask==15;});
    host_tasks::pause_at_boundary();assert(http_fixture::control_calls.load()==1);
    host_tasks::resume();
  }
  host_tasks::join_all();
}

/** Shared Retry-After suppresses GET and commands across logout and a new grant. */
void shared_rate_limit() {
  reset();
  {
    Api api;boot(api);const int players=http_fixture::player_calls.load();
    http_fixture::control_status=429;http_fixture::control_headers={{"Retry-After","600"}};
    assert(api.request_play_pause());host_tasks::resume();
    wait_until([](){return http_fixture::control_calls.load()==1;});host_tasks::pause_at_boundary();
    assert(http_fixture::player_calls.load()==players);
    usp_spotify_controls::ControlView view{};assert(api.copy_controls(&view));
    assert(view.outcome==usp_spotify_controls::Outcome::RATE_LIMITED && view.allowed_mask==0);
    post(api,"cancel","",200);grant(api);host_tasks::resume();
    wait_until([&](){return api.connections.load()==2;});host_tasks::pause_at_boundary();
    assert(http_fixture::player_calls.load()==players && !api.request_play_pause());
    esphome::host_time+=599999;host_tasks::resume();
    std::this_thread::sleep_for(std::chrono::milliseconds(8));host_tasks::pause_at_boundary();
    assert(http_fixture::player_calls.load()==players);
    esphome::host_time+=1;host_tasks::resume();
    wait_until([&](){return api.copy_controls(&view) && view.allowed_mask==15;});
    host_tasks::pause_at_boundary();assert(http_fixture::control_calls.load()==1);
    host_tasks::resume();
  }
  host_tasks::join_all();
}

/** Logout during HTTPS cannot assign an old GET or401 to the next token owner. */
void cancelled_flight() {
  reset();
  {
    Api api;boot(api);const int players=http_fixture::player_calls.load();
    http_fixture::control_status=401;
    http_fixture::before_control=[&](){
      assert(!api.request_play_pause());
      post(api,"cancel","",200);grant(api);
      usp_spotify_controls::ControlView view{};assert(api.copy_controls(&view));
      assert(view.allowed_mask==0);
    };
    assert(api.request_play_pause());host_tasks::resume();
    wait_until([&](){return api.connections.load()==2;});host_tasks::pause_at_boundary();
    assert(status(api).find("connected")!=std::string::npos);
    assert(http_fixture::control_calls.load()==1 && http_fixture::token_calls.load()==2);
    assert(http_fixture::player_calls.load()==players+1);
    usp_spotify_controls::ControlView view{};assert(api.copy_controls(&view));
    assert(view.allowed_mask==15);
    host_tasks::resume();
  }
  host_tasks::join_all();
}


/** The actual stored grant, rather than the requested scope list, enables buttons. */
void missing_grant() {
  reset();
  http_fixture::response=R"({"access_token":"read_only_A","token_type":"Bearer","expires_in":3600,"refresh_token":"fixture-refresh","scope":"user-read-playback-state"})";
  {
    Api api;configure_application(api);api.setup();grant(api);host_tasks::release();
    wait_until([](){return http_fixture::player_calls.load()==1;});host_tasks::pause_at_boundary();
    usp_spotify_controls::ControlView view{};assert(api.copy_controls(&view));
    assert(view.allowed_mask==0 && !api.request_play_pause());
    assert(!api.request_control(usp_spotify_controls::Command::NEXT));
    assert(http_fixture::control_calls.load()==0);
    host_tasks::resume();
  }
  host_tasks::join_all();
}

/** Reject publication of old-account metadata while a new grant is pending. */
class EpochApi : public Api {
 public:
  using Api::publish;
  std::atomic<int> rejected_old{0};
  bool publish(uint32_t generation,const usp_spotify_playback_worker::PlaybackState &state) override {
    const bool accepted=ProductionApi::publish(generation,state);
    if(std::string(state.snapshot.item_name.data())=="Old session") {
      assert(!accepted);++rejected_old;
    }
    return accepted;
  }
};

/** Cancellation inside GET cannot promote its old bearer or metadata to a new account. */
void cancelled_get() {
  reset();
  {
    EpochApi api;boot(api);bool cancelled=false;
    auto &body=http_fixture::player_response;
    body.replace(body.find("Bound track"),11,"Old session");
    http_fixture::before_perform=[&](){
      if(http_fixture::config.method==HTTP_METHOD_GET && !cancelled) {
        cancelled=true;post(api,"cancel","",200);grant(api);
      } else if(cancelled && http_fixture::config.method==HTTP_METHOD_POST) {
        body.replace(body.find("Old session"),11,"New session");
      }
    };
    esphome::host_time+=5000;host_tasks::resume();
    wait_until([&](){return api.connections.load()==2;});host_tasks::pause_at_boundary();
    assert(api.rejected_old.load()>=1);
    assert(http_fixture::control_calls.load()==0 && http_fixture::token_calls.load()==2);
    usp_spotify_playback_worker::PlaybackState state{};assert(api.copy_playback(&state));
    assert(std::string(state.snapshot.item_name.data())=="New session");
    assert(status(api).find("connected")!=std::string::npos);
    host_tasks::resume();
  }
  host_tasks::join_all();
}

int main() {
  esphome::web_server_base::WebServerBase base;
  esphome::web_server_base::global_web_server_base=&base;
  four_commands();expired_tap();uncertain_command();shared_rate_limit();cancelled_flight();
  missing_grant();cancelled_get();
  std::cout<<"SPOTIFY_CONTROLS_BINDING_NATIVE=PASS actual_task actual_parser fixed4 explicit_tap burst expired uncertain_no_replay shared429_logout stale401 owned_token no_network\n";
}
