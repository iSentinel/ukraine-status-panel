#include "token_worker_engine.h"
#include <cassert>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
using namespace usp_spotify_tokens;
using usp_spotify_session::LoadState;
using usp_spotify_session::Session;
using usp_spotify_session::PersistenceBridge;
constexpr const char *CODE_GOOD=R"({"access_token":"access_A","token_type":"Bearer","expires_in":3600,"refresh_token":"refresh_A","scope":"user-read-playback-state user-modify-playback-state"})";
constexpr const char *REFRESH_SAME=R"({"access_token":"access_B","token_type":"Bearer","expires_in":3600})";
constexpr const char *REFRESH_ROTATED=R"({"access_token":"access_C","token_type":"Bearer","expires_in":3600,"refresh_token":"refresh_C"})";
constexpr const char *INVALID_GRANT=R"({"error":"invalid_grant"})";

struct FakeClock : Clock {uint32_t value{100};uint32_t now() const override{return value;}};
struct FakeBridge : Bridge {
  uint32_t epoch{0},accepted{100};bool queued{false},blocked{false};Phase phase{Phase::IDLE};
  uint32_t generation() const override{return epoch;}
  bool take(usp_spotify_auth_return::TokenRequest *out,uint32_t *generation,uint32_t *accepted_at) override {
    if(!queued||blocked)return false;
    queued=false;out->clear();
    usp_spotify_auth_return::copy_field(out->code,"fixture-code");
    usp_spotify_auth_return::copy_field(out->verifier,std::string(43,'a'));
    usp_spotify_auth_return::copy_field(out->client_id,"client123");
    usp_spotify_auth_return::copy_field(out->redirect_uri,"https://return.test/cb");
    *generation=epoch;*accepted_at=accepted;return true;
  }
  bool publish(uint32_t generation,Phase next) override {
    if(blocked||generation!=epoch)return false;
    phase=next;return true;
  }
};
struct Answer{HttpResult result;std::string body;};
struct FakeTransport : Transport {
  std::vector<Answer> answers;std::vector<std::string> forms;std::function<void()> during;size_t at{0};
  HttpResult perform(const char *body,size_t size,char *output,size_t capacity) override {
    forms.emplace_back(body,size);assert(at<answers.size());auto a=answers[at++];assert(a.body.size()<=capacity);
    std::memcpy(output,a.body.data(),a.body.size());a.result.size=a.body.size();if(during)during();return a.result;
  }
};
Answer ok(const char *body){return {{true,true,200,0,false,false,0},body};}
Answer invalid(){return {{true,true,400,0,false,false,0},INVALID_GRANT};}

struct FakePersistence : PersistenceBridge {
  uint32_t epoch{0};bool present{false},fail_save{false},fail_clear{false},key_unavailable{false},corrupt{false};
  unsigned loads{0},saves{0},clears{0};Session value;
  void set(const char *refresh="persist_R",const char *client="client123",const char *scope="user-read-playback-state") {
    value.clear();usp_spotify_auth_return::copy_field(value.refresh,refresh);
    usp_spotify_auth_return::copy_field(value.client,client);usp_spotify_auth_return::copy_field(value.scope,scope);present=true;
  }
  void transition(uint32_t next,bool erase=true){epoch=next;if(erase){value.clear();present=false;}}
  LoadState load(uint32_t generation,Session *out) override {
    ++loads;if(generation!=epoch)return LoadState::IO_ERROR;if(corrupt)return LoadState::CORRUPT;
    if(key_unavailable && present)return LoadState::KEY_UNAVAILABLE;
    if(!present)return LoadState::EMPTY;
    out->clear();std::memcpy(out->refresh.data(),value.refresh.data(),value.refresh.size());
    std::memcpy(out->client.data(),value.client.data(),value.client.size());
    std::memcpy(out->scope.data(),value.scope.data(),value.scope.size());return LoadState::READY;
  }
  bool save(uint32_t generation,std::string_view refresh,std::string_view client,std::string_view scope) override {
    ++saves;if(generation!=epoch||fail_save)return false;set(std::string(refresh).c_str(),std::string(client).c_str(),std::string(scope).c_str());return true;
  }
  bool clear(uint32_t generation) override {
    ++clears;if(generation!=epoch||fail_clear)return false;value.clear();present=false;return true;
  }
};
void pump(Engine &e,FakeBridge &b,FakeTransport &t,FakeClock &c,FakePersistence &p,int steps=4){for(int i=0;i<steps;++i)e.step(b,t,c,&p);}

void startup_restore() {
  Engine e;FakeBridge b;FakeClock c;FakeTransport t;FakePersistence p;p.set();t.answers={ok(REFRESH_SAME)};
  pump(e,b,t,c,p,4);assert(t.forms.size()==1&&b.phase==Phase::CONNECTED);
  assert(t.forms[0].find("grant_type=refresh_token")!=std::string::npos);
  assert(t.forms[0].find("refresh_token=persist_R")!=std::string::npos);
  assert(e.access(c.value)=="access_B"&&p.saves==0&&p.loads==1);
}
void code_persist_and_rotation() {
  Engine e;FakeBridge b;FakeClock c;FakeTransport t;FakePersistence p;b.queued=true;
  t.answers={ok(CODE_GOOD),ok(REFRESH_SAME),ok(REFRESH_ROTATED)};
  pump(e,b,t,c,p,4);assert(b.phase==Phase::CONNECTED&&p.saves==1&&p.present);
  assert(std::string(p.value.refresh.data())=="refresh_A");
  c.value=3540100;pump(e,b,t,c,p,4);assert(p.saves==1&&std::string(p.value.refresh.data())=="refresh_A");
  c.value=7080100;pump(e,b,t,c,p,4);assert(p.saves==2&&std::string(p.value.refresh.data())=="refresh_C");
}
void save_retry() {
  Engine e;FakeBridge b;FakeClock c;FakeTransport t;FakePersistence p;b.queued=true;p.fail_save=true;t.answers={ok(CODE_GOOD)};
  pump(e,b,t,c,p,4);assert(b.phase==Phase::CONNECTED&&p.saves==1&&!p.present);
  c.value+=29999;pump(e,b,t,c,p,3);assert(p.saves==1);
  p.fail_save=false;c.value+=1;pump(e,b,t,c,p,2);assert(p.saves==2&&p.present);
}
void invalid_grant_clears() {
  Engine e;FakeBridge b;FakeClock c;FakeTransport t;FakePersistence p;p.set();t.answers={invalid()};
  pump(e,b,t,c,p,4);assert(b.phase==Phase::REAUTH&&!p.present&&p.clears==1&&t.forms.size()==1);
  c.value+=600000;pump(e,b,t,c,p,30);assert(t.forms.size()==1);
}
void failed_clear_retries() {
  Engine e;FakeBridge b;FakeClock c;FakeTransport t;FakePersistence p;p.set();p.fail_clear=true;t.answers={invalid()};
  pump(e,b,t,c,p,4);assert(b.phase==Phase::REAUTH&&p.present&&p.clears==1);
  c.value+=29999;pump(e,b,t,c,p,3);assert(p.clears==1);
  p.fail_clear=false;c.value+=1;pump(e,b,t,c,p,2);assert(p.clears==2&&!p.present);
}
void explicit_transition_no_resurrection() {
  Engine e;FakeBridge b;FakeClock c;FakeTransport t;FakePersistence p;p.set();
  // Adapter semantics: clear persisted record under its persistence lock, then advance generation.
  assert(p.clear(0));b.epoch=1;p.epoch=1;p.present=false;
  pump(e,b,t,c,p,20);assert(t.forms.empty()&&b.phase==Phase::IDLE&&!p.present);
  // BEGIN has the same clear-before-generation rule; a new code may then create a new session.
  p.set("old_R");assert(p.clear(1));b.epoch=2;p.epoch=2;b.queued=true;t.answers={ok(CODE_GOOD)};
  pump(e,b,t,c,p,4);assert(p.present&&std::string(p.value.refresh.data())=="refresh_A");
}
void cancellation_inflight_never_saves_old() {
  Engine e;FakeBridge b;FakeClock c;FakeTransport t;FakePersistence p;b.queued=true;t.answers={ok(CODE_GOOD)};
  // Simulate adapter CANCEL while token HTTPS is in flight: clear vault and advance epoch.
  t.during=[&](){assert(p.clear(0));p.epoch=1;b.epoch=1;b.phase=Phase::IDLE;};
  pump(e,b,t,c,p,6);assert(p.saves==0&&!p.present&&b.phase==Phase::IDLE);
}
void force_reauth_clears() {
  Engine e;FakeBridge b;FakeClock c;FakeTransport t;FakePersistence p;b.queued=true;t.answers={ok(CODE_GOOD)};
  pump(e,b,t,c,p,4);assert(p.present);e.force_reauth(&p);e.step(b,t,c,&p);
  assert(!p.present&&b.phase==Phase::REAUTH&&e.access(c.value).empty());
}
void key_and_corrupt_fail_closed() {
  for(int mode=0;mode<2;++mode){Engine e;FakeBridge b;FakeClock c;FakeTransport t;FakePersistence p;p.set();
    if(mode==0)p.key_unavailable=true;else p.corrupt=true;pump(e,b,t,c,p,4);
    assert(b.phase==Phase::REAUTH&&t.forms.empty());if(mode==1)assert(!p.present);
  }
}
int main(){startup_restore();code_persist_and_rotation();save_retry();invalid_grant_clears();failed_clear_retries();
  explicit_transition_no_resurrection();cancellation_inflight_never_saves_old();force_reauth_clears();key_and_corrupt_fail_closed();
  std::cout<<"SPOTIFY_SESSION_LIFECYCLE_NATIVE=PASS startup_restore code_save no_rotation rotation retry invalid_grant_clear clear_retry logout_begin_no_resurrection stale_http player401 key_fail no_network\n";}
