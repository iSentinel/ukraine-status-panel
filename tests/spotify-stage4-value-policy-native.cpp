#include "value_control_policy.h"
#include <ArduinoJson.h>
#include <cassert>
#include <cstdint>
#include <iostream>
using namespace usp_spotify_value_controls;

Facts facts(uint32_t at, bool volume=true, bool seek_known=true, bool seek_allowed=true) {
  Facts f{};f.updated_at=at;f.duration_ms=240000;f.valid=true;f.active=true;f.restricted=false;
  f.supports_volume=volume;f.has_item=true;f.seek={seek_known,seek_allowed};return f;
}

void seek_parser() {
  JsonDocument doc;assert(deserializeJson(doc,R"({"disallows":{"seeking":false}})")==DeserializationError::Ok);
  auto p=seek_permission(doc.as<JsonVariantConst>());assert(p.known&&p.allowed);
  assert(deserializeJson(doc,R"({"disallows":{"seeking":true}})")==DeserializationError::Ok);
  p=seek_permission(doc.as<JsonVariantConst>());assert(p.known&&!p.allowed);
  assert(deserializeJson(doc,R"({"pausing":false})")==DeserializationError::Ok);
  p=seek_permission(doc.as<JsonVariantConst>());assert(!p.known&&!p.allowed);
  assert(deserializeJson(doc,R"({"disallows":7})")==DeserializationError::Ok);
  p=seek_permission(doc.as<JsonVariantConst>());assert(!p.known&&!p.allowed);
}

void policy() {
  constexpr auto scope="user-read-playback-state user-modify-playback-state";
  Plan plan;plan.session(7,scope);plan.facts(7,facts(100));
  assert(plan.request_volume(7,0,101));Request req{};assert(plan.take(7,102,&req));
  assert(req.kind==Kind::VOLUME&&req.value==0&&plan.current(7,req,103));
  assert(plan.finish(req,204,true)&&plan.outcome()==Outcome::CONFIRMED);
  plan.facts(7,facts(200));assert(plan.request_volume(7,100,201));assert(plan.take(7,202,&req));
  assert(plan.finish(req,429,true)&&plan.outcome()==Outcome::RATE_LIMIT);
  plan.facts(7,facts(300));assert(plan.request_volume(7,101,301)==0);
  plan.facts(7,facts(400,false));assert(plan.request_volume(7,50,401)==0);

  plan.facts(7,facts(500));assert(plan.request_seek(7,1,501));assert(plan.take(7,502,&req));
  assert(req.kind==Kind::SEEK&&req.value==1);assert(plan.finish(req,204,true));
  plan.facts(7,facts(600));assert(plan.request_seek(7,239999,601));assert(plan.take(7,602,&req));
  assert(plan.finish(req,403,true)&&plan.outcome()==Outcome::FORBIDDEN);
  plan.facts(7,facts(700));assert(plan.request_seek(7,0,701)==0);
  assert(plan.request_seek(7,240000,701)==0&&plan.request_seek(7,240001,701)==0);
  plan.facts(7,facts(800,true,false,false));assert(plan.request_seek(7,1000,801)==0);
  plan.facts(7,facts(900,true,true,false));assert(plan.request_seek(7,1000,901)==0);

  auto stale=facts(1000);plan.facts(7,stale);assert(plan.request_volume(7,50,1000+FACTS_TTL_MS)==0);
  plan.facts(7,facts(1100));assert(plan.request_seek(7,1000,1101));assert(plan.take(7,1102,&req));
  // New facts forbid a not-yet-dispatched stale request. If HTTPS was already
  // attempted, however, its matching response must still be recorded so the
  // command is never replayed because its outcome was discarded.
  plan.facts(7,facts(1103));assert(!plan.current(7,req,1104));
  assert(plan.finish(req,204,true)&&plan.outcome()==Outcome::CONFIRMED);

  Plan no_scope;no_scope.session(1,"user-read-playback-state");no_scope.facts(1,facts(10));
  assert(no_scope.request_volume(1,50,11)==0&&no_scope.request_seek(1,1000,11)==0);

  Plan wrap;wrap.session(2,scope);wrap.facts(2,facts(UINT32_MAX-5));
  assert(wrap.request_seek(2,1000,UINT32_MAX-4));assert(wrap.take(2,UINT32_MAX-3,&req));
  assert(wrap.current(2,req,1));assert(wrap.finish(req,401,true)&&wrap.outcome()==Outcome::REAUTH);
}

void stress() {
  constexpr auto scope="user-modify-playback-state";
  for(uint32_t i=1;i<=10000;++i){Plan p;p.session(i,scope);p.facts(i,facts(i*13U));
    const uint32_t now=i*13U+1;const bool volume=(i&1U)!=0;
    assert(volume?p.request_volume(i,i%101,now):p.request_seek(i,1+(i%200000),now));
    Request r{};assert(p.take(i,now+1,&r));assert(p.current(i,r,now+2));assert(p.finish(r,204,true));
    assert(p.outcome()==Outcome::CONFIRMED);}
}

int main(){seek_parser();policy();stress();std::cout<<"SPOTIFY_VOLUME_SEEK_POLICY_NATIVE=PASS bounds no_clamp supports_volume seek_actions freshness revision one_shot no_replay wrap scope stress10000 no_network\n";}
