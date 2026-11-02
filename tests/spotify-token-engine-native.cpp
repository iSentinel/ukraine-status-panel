#include "token_worker_engine.h"
#include <cassert>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
using namespace usp_spotify_tokens;
constexpr const char *GOOD=R"({"access_token":"access_A","token_type":"Bearer","expires_in":3600,"refresh_token":"old+refresh","scope":"user-read-playback-state"})";
constexpr const char *ROTATION_ABSENT=R"({"access_token":"access_B","token_type":"Bearer","expires_in":3600})";

struct FakeClock : Clock {
  uint32_t value{100};
  uint32_t now() const override {return value;}
};
struct FakeBridge : Bridge {
  uint32_t epoch{1},accepted{100};
  bool queued{true},blocked{false};
  int taken{0},published{0};
  Phase phase{Phase::IDLE};
  uint32_t generation() const override {return epoch;}
  bool take(usp_spotify_auth_return::TokenRequest *out,uint32_t *generation,uint32_t *accepted_at) override {
    if(!queued || blocked)return false;
    out->clear();
    usp_spotify_auth_return::copy_field(out->code,"fixture-code");
    usp_spotify_auth_return::copy_field(out->verifier,std::string(43,'a'));
    usp_spotify_auth_return::copy_field(out->client_id,"client123");
    usp_spotify_auth_return::copy_field(out->redirect_uri,"https://return.test/cb");
    *generation=epoch;*accepted_at=accepted;queued=false;++taken;return true;
  }
  bool publish(uint32_t generation,Phase next) override {
    if(blocked || generation!=epoch)return false;
    ++published;phase=next;return true;
  }
};
struct Answer {HttpResult result;std::string body;};
struct FakeTransport : Transport {
  std::vector<Answer> answers;
  std::vector<std::string> forms;
  std::function<void()> during;
  size_t at{0};
  HttpResult perform(const char *body,size_t size,char *output,size_t capacity) override {
    forms.emplace_back(body,size);assert(at<answers.size());
    auto answer=answers[at++];
    assert(answer.body.size()<=capacity);
    std::memcpy(output,answer.body.data(),answer.body.size());
    answer.result.size=answer.body.size();
    if(during)during();
    return answer.result;
  }
};
Answer success(std::string body=GOOD) {return {{true,true,200,0,false,false,0},std::move(body)};}
void pump(Engine &e,FakeBridge &b,FakeTransport &t,FakeClock &c,int steps=3) {
  for(int i=0;i<steps;++i)e.step(b,t,c);
}

/** Refresh without rotation keeps the old refresh token and public client id. */
void lifecycle() {
  Engine e;FakeBridge b;FakeClock c;FakeTransport t;
  t.answers={success(),success(ROTATION_ABSENT),success(ROTATION_ABSENT)};
  pump(e,b,t,c);assert(b.phase==Phase::CONNECTED && t.forms.size()==1);
  c.value=3540100;pump(e,b,t,c);
  assert(b.phase==Phase::CONNECTED && t.forms.size()==2);
  assert(t.forms[1].find("refresh_token=old%2Brefresh")!=std::string::npos);
  c.value=7080100;pump(e,b,t,c);
  assert(b.phase==Phase::CONNECTED && t.forms.size()==3);
  assert(t.forms[2].find("client_id=client123")!=std::string::npos);
  assert(t.forms[2].find("refresh_token=old%2Brefresh")!=std::string::npos);
}

/** An attempted code with no confirmed response is never sent again. */
void ambiguous_code() {
  Engine e;FakeBridge b;FakeClock c;FakeTransport t;
  t.answers={{{true,false,0,0,false,false,0},""}};
  pump(e,b,t,c);pump(e,b,t,c,300);
  assert(b.phase==Phase::REAUTH && t.forms.size()==1 && b.taken==1);
}

/** Cancellation while HTTP runs cannot restore a connected state. */
void cancellation() {
  Engine e;FakeBridge b;FakeClock c;FakeTransport t;
  t.answers={success()};
  t.during=[&](){++b.epoch;b.phase=Phase::IDLE;};
  pump(e,b,t,c);pump(e,b,t,c,30);
  assert(b.phase==Phase::IDLE && t.forms.size()==1);
  Engine before;FakeBridge b2;FakeTransport t2;
  before.step(b2,t2,c);++b2.epoch;
  pump(before,b2,t2,c);
  assert(t2.forms.empty());
}

/** Busy publication defers only publication; token HTTPS is not repeated. */
void busy_publication() {
  Engine e;FakeBridge b;FakeClock c;FakeTransport t;t.answers={success()};
  pump(e,b,t,c,2);b.blocked=true;
  pump(e,b,t,c,300);assert(t.forms.size()==1);
  b.blocked=false;e.step(b,t,c);
  assert(b.phase==Phase::CONNECTED && t.forms.size()==1);
  Engine expired;FakeBridge b2;FakeTransport t2;
  t2.answers={success(R"({"access_token":"short","token_type":"Bearer","expires_in":1,"refresh_token":"refresh"})")};
  pump(expired,b2,t2,c,2);b2.blocked=true;c.value=1100;
  expired.step(b2,t2,c);b2.blocked=false;expired.step(b2,t2,c);
  assert(b2.phase==Phase::EXCHANGING && t2.forms.size()==1);
}

/** The original callback deadline applies even while admission is deferred. */
void deadlines() {
  Engine e;FakeBridge b;FakeClock c;FakeTransport t;
  b.accepted=0;c.value=59999;e.step(b,t,c);
  c.value=60000;pump(e,b,t,c);
  assert(b.phase==Phase::REAUTH && t.forms.empty());
}

/** Provider cooldown is independent of new sign-in generations. */
void rate_limit() {
  Engine e;FakeBridge b;FakeClock c;FakeTransport t;
  t.answers={{{true,true,429,0,true,false,120000},""},success()};
  pump(e,b,t,c);assert(b.phase==Phase::REAUTH && t.forms.size()==1);
  ++b.epoch;b.queued=true;b.accepted=10000;c.value=10000;
  pump(e,b,t,c);assert(t.forms.size()==1);
  c.value=70000;pump(e,b,t,c);assert(b.phase==Phase::REAUTH && t.forms.size()==1);
  ++b.epoch;b.queued=true;b.accepted=130101;c.value=130101;
  pump(e,b,t,c);assert(b.phase==Phase::CONNECTED && t.forms.size()==2);
}

/** No-I/O admission failure may retry; it has not attempted the one-time code. */
void admission() {
  Engine e;FakeBridge b;FakeClock c;FakeTransport t;
  t.answers={{{false,false,0,0,false,false,0},""},success()};
  pump(e,b,t,c);assert(t.forms.size()==1 && b.phase==Phase::EXCHANGING);
  c.value=599;pump(e,b,t,c);assert(t.forms.size()==1);
  c.value=600;pump(e,b,t,c);assert(t.forms.size()==2 && b.phase==Phase::CONNECTED);
}

/** invalid_grant stops refresh instead of creating a repeated failure loop. */
void invalid_grant() {
  Engine e;FakeBridge b;FakeClock c;FakeTransport t;
  t.answers={success(),{{true,true,400,0,false,false,0},R"({"error":"invalid_grant"})"}};
  pump(e,b,t,c);c.value=3540100;pump(e,b,t,c);
  assert(b.phase==Phase::REAUTH && t.forms.size()==2);
  c.value+=600000;pump(e,b,t,c,300);assert(t.forms.size()==2);
}
int main() {
  lifecycle();ambiguous_code();cancellation();busy_publication();deadlines();
  rate_limit();admission();invalid_grant();
  std::cout<<"SPOTIFY_TOKEN_ENGINE_PREFLIGHT=PASS fake_transport code_once refresh_retained generation_cancel busy300 expiry_publish grant_deadline retry_after_no_bypass invalid_grant no_network\n";
}
