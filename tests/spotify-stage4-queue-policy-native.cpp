#include "queue_policy.h"
#include <ArduinoJson.h>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
using namespace usp_spotify_queue;
std::string track(std::string id="ABC123",std::string name="Song"){return "{\"type\":\"track\",\"uri\":\"spotify:track:"+id+"\",\"name\":\""+name+"\",\"duration_ms\":123000,\"artists\":[{\"name\":\"Artist\"}]}";}
std::string episode(std::string id="EP123"){return "{\"type\":\"episode\",\"uri\":\"spotify:episode:"+id+"\",\"name\":\"Episode\",\"duration_ms\":456000,\"show\":{\"name\":\"Show\"}}";}
Queue parsed(const std::string&body){JsonDocument doc;assert(deserializeJson(doc,body)==DeserializationError::Ok);Queue q{};assert(parse(doc.as<JsonObjectConst>(),&q));return q;}
void parser(){auto q=parsed("{\"currently_playing\":"+track()+",\"queue\":["+track("DUP")+","+track("DUP")+","+episode()+"]}");assert(q.valid&&q.has_current&&q.count==3&&!q.truncated);assert(q.items[0].uri==q.items[1].uri);assert(q.items[2].type==ItemType::EPISODE);
auto empty=parsed(R"({"currently_playing":null,"queue":[]})");assert(empty.valid&&!empty.has_current&&empty.count==0);
std::string many="{\"currently_playing\":null,\"queue\":[";for(int i=0;i<20;++i){if(i)many+=",";many+=track("T"+std::to_string(i));}many+="]}";auto bounded=parsed(many);assert(bounded.count==MAX_ITEMS&&bounded.truncated);
JsonDocument bad;assert(deserializeJson(bad,R"({"currently_playing":null,"queue":[{"type":"track","uri":"https://evil/","name":"x","artists":[{"name":"a"}]}]})")==DeserializationError::Ok);Queue fail{};assert(!parse(bad.as<JsonObjectConst>(),&fail));
for(auto good:{"spotify:track:A","spotify:episode:AbC123"}) assert(spotify_uri(good));
for(auto baduri:{"spotify:album:A","spotify:track:","spotify:track:A/B","https://open.spotify.com/track/A","spotify:local:A"}) assert(!spotify_uri(baduri));
}
Facts facts(uint32_t at){return {at,true,true,false};}
void policy(){constexpr auto scope="user-modify-playback-state";AddPlan p;p.session(7,scope);p.facts(7,facts(100));AddRequest r{};assert(p.request(7,"spotify:track:ABC123",101));assert(p.take(7,102,&r));assert(p.current(7,r,103));assert(p.finish(r,204,true)&&p.outcome()==Outcome::CONFIRMED);p.facts(7,facts(200));assert(p.request(7,"spotify:episode:EP123",201));assert(p.take(7,202,&r));assert(p.finish(r,429,true)&&p.outcome()==Outcome::RATE_LIMIT);p.facts(7,facts(300));assert(p.request(7,"https://evil",301)==0);auto stale=facts(400);p.facts(7,stale);assert(p.request(7,"spotify:track:A",400+QUEUE_TTL_MS)==0);p.facts(7,facts(500));assert(p.request(7,"spotify:track:A",501));assert(p.take(7,502,&r));p.facts(7,facts(503));assert(!p.current(7,r,504));assert(p.finish(r,204,true));AddPlan no_scope;no_scope.session(1,"user-read-currently-playing user-read-playback-state");no_scope.facts(1,facts(10));assert(no_scope.request(1,"spotify:track:A",11)==0);AddPlan wrap;wrap.session(2,scope);wrap.facts(2,facts(UINT32_MAX-5));assert(wrap.request(2,"spotify:track:A",UINT32_MAX-4));assert(wrap.take(2,UINT32_MAX-3,&r));assert(wrap.current(2,r,1));assert(wrap.finish(r,401,true)&&wrap.outcome()==Outcome::REAUTH);}
void stress(){constexpr auto scope="user-modify-playback-state";for(uint32_t i=1;i<=10000;++i){AddPlan p;p.session(i,scope);p.facts(i,facts(i*13U));const uint32_t now=i*13U+1;assert(p.request(i,(i&1U)?"spotify:track:A":"spotify:episode:B",now));AddRequest r{};assert(p.take(i,now+1,&r));assert(p.current(i,r,now+2));assert(p.finish(r,204,true));}}
int main(){parser();policy();stress();std::cout<<"SPOTIFY_QUEUE_POLICY_NATIVE=PASS current_nullable tracks_episodes duplicate_allowed bound12 strict_uri fresh active scope one_shot no_replay wrap stress10000 no_network\n";}
