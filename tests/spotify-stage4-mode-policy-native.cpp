#include "mode_control_policy.h"
#include <ArduinoJson.h>
#include <cassert>
#include <cstdint>
#include <iostream>
using namespace usp_spotify_modes;

Facts facts(uint32_t at,bool shuffle=false,Repeat repeat=Repeat::OFF,bool allow_shuffle=true,bool allow_context=true,bool allow_track=true){Facts f{};f.updated_at=at;f.valid=true;f.active=true;f.restricted=false;f.shuffle=shuffle;f.repeat=repeat;f.shuffle_permission={true,allow_shuffle};f.repeat_context={true,allow_context};f.repeat_track={true,allow_track};return f;}

void parser(){JsonDocument doc;Facts f{};
 assert(deserializeJson(doc,R"({"shuffle_state":false,"repeat_state":"context","device":{"is_active":true,"is_restricted":false},"actions":{"disallows":{"toggling_shuffle":false,"toggling_repeat_context":false,"toggling_repeat_track":true}}})")==DeserializationError::Ok);
 assert(parse_facts(doc.as<JsonObjectConst>(),7,&f));assert(!f.shuffle&&f.repeat==Repeat::CONTEXT&&f.shuffle_permission.allowed&&f.repeat_context.allowed&&!f.repeat_track.allowed);
 assert(deserializeJson(doc,R"({"shuffle_state":true,"repeat_state":"bad","device":{"is_active":true,"is_restricted":false},"actions":{"disallows":{}}})")==DeserializationError::Ok);assert(!parse_facts(doc.as<JsonObjectConst>(),8,&f));
}

void policy(){constexpr auto scope="user-modify-playback-state";Plan p;p.session(7,scope);Request r{};
 p.facts(7,facts(100,false));assert(p.request_shuffle_toggle(7,101));assert(p.take(7,102,&r));assert(r.kind==Kind::SHUFFLE&&r.shuffle&&p.current(7,r,103));assert(p.finish(r,204,true)&&p.outcome()==Outcome::CONFIRMED);
 p.facts(7,facts(200,true));assert(p.request_shuffle_toggle(7,201));assert(p.take(7,202,&r));assert(!r.shuffle);assert(p.finish(r,429,true)&&p.outcome()==Outcome::RATE_LIMIT);
 p.facts(7,facts(300,false,Repeat::OFF,false));assert(p.request_shuffle_toggle(7,301)==0);
 p.facts(7,facts(400,false,Repeat::OFF));assert(p.request_repeat(7,Repeat::CONTEXT,401));assert(p.take(7,402,&r));assert(r.repeat==Repeat::CONTEXT);assert(p.finish(r,204,true));
 p.facts(7,facts(500,false,Repeat::CONTEXT));assert(p.request_repeat(7,Repeat::TRACK,501));assert(p.take(7,502,&r));assert(p.finish(r,403,true)&&p.outcome()==Outcome::FORBIDDEN);
 p.facts(7,facts(600,false,Repeat::TRACK));assert(p.request_repeat(7,Repeat::OFF,601));assert(p.take(7,602,&r));assert(p.finish(r,204,true));
 p.facts(7,facts(700,false,Repeat::OFF,true,false,true));assert(p.request_repeat(7,Repeat::CONTEXT,701)==0);assert(p.request_repeat(7,Repeat::TRACK,701));assert(p.take(7,702,&r));assert(p.finish(r,204,true));
 p.facts(7,facts(800,false,Repeat::CONTEXT,true,true,false));assert(p.request_repeat(7,Repeat::TRACK,801)==0);assert(p.request_repeat(7,Repeat::OFF,801));assert(p.take(7,802,&r));assert(p.finish(r,204,true));
 p.facts(7,facts(900));assert(p.request_repeat(7,Repeat::OFF,901)==0);
 auto stale=facts(1000);p.facts(7,stale);assert(p.request_shuffle_toggle(7,1000+FACTS_TTL_MS)==0);
 p.facts(7,facts(1100));assert(p.request_shuffle_toggle(7,1101));assert(p.take(7,1102,&r));p.facts(7,facts(1103));assert(!p.current(7,r,1104));assert(p.finish(r,204,true)&&p.outcome()==Outcome::CONFIRMED);
 Plan no_scope;no_scope.session(1,"user-read-playback-state");no_scope.facts(1,facts(10));assert(no_scope.request_shuffle_toggle(1,11)==0&&no_scope.request_repeat(1,Repeat::TRACK,11)==0);
 Plan wrap;wrap.session(2,scope);wrap.facts(2,facts(UINT32_MAX-5));assert(wrap.request_repeat(2,Repeat::CONTEXT,UINT32_MAX-4));assert(wrap.take(2,UINT32_MAX-3,&r));assert(wrap.current(2,r,1));assert(wrap.finish(r,401,true)&&wrap.outcome()==Outcome::REAUTH);
}

void stress(){constexpr auto scope="user-modify-playback-state";for(uint32_t i=1;i<=10000;++i){Plan p;p.session(i,scope);p.facts(i,facts(i*11U,(i&1U)!=0));const uint32_t now=i*11U+1;if(i&1U)assert(p.request_shuffle_toggle(i,now));else assert(p.request_repeat(i,Repeat::CONTEXT,now));Request r{};assert(p.take(i,now+1,&r));assert(p.current(i,r,now+2));assert(p.finish(r,204,true));assert(p.outcome()==Outcome::CONFIRMED);}}
int main(){parser();policy();stress();std::cout<<"SPOTIFY_SHUFFLE_REPEAT_POLICY_NATIVE=PASS parse exact_states fresh_actions set_state no_noop no_replay wrap scope stress10000 no_network\n";}
