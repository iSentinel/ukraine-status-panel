#include "device_transfer_plan.h"
#include <ArduinoJson.h>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
using namespace usp_spotify_devices;

Catalog parsed(const std::string &body) {
  JsonDocument doc;
  assert(deserializeJson(doc, body) == DeserializationError::Ok);
  Catalog out{};
  assert(parse(doc.as<JsonObjectConst>(), &out));
  return out;
}

std::string device(std::string id, bool active=false, bool restricted=false,
                   bool private_session=false, int volume=59) {
  const std::string volume_json = volume < 0 ? "null" : std::to_string(volume);
  return "{\"id\":" + (id.empty() ? "null" : "\"" + id + "\"") +
      ",\"is_active\":" + (active ? "true" : "false") +
      ",\"is_private_session\":" + (private_session ? "true" : "false") +
      ",\"is_restricted\":" + (restricted ? "true" : "false") +
      ",\"name\":\"Kitchen speaker\",\"type\":\"speaker\",\"volume_percent\":" +
      volume_json + ",\"supports_volume\":true}";
}

void parser_cases() {
  auto list=parsed("{\"devices\":["+device("A",true)+","+device("B")+","+device("")+","+device("C",false,true)+"]}");
  assert(list.valid && list.count==4 && !list.truncated);
  assert(list.devices[0].active && list.devices[0].selectable);
  assert(list.devices[1].selectable && !list.devices[1].restricted);
  assert(!list.devices[2].selectable && list.devices[2].id[0]==0);
  assert(!list.devices[3].selectable && list.devices[3].restricted);
  auto nullable=parsed("{\"devices\":["+device("P",false,false,true,-1)+"]}");
  assert(nullable.devices[0].private_session && nullable.devices[0].volume_percent==VOLUME_UNKNOWN);

  JsonDocument duplicate_doc;
  assert(deserializeJson(duplicate_doc,"{\"devices\":["+device("X")+","+device("X")+"]}")==DeserializationError::Ok);
  Catalog duplicate{};assert(!parse(duplicate_doc.as<JsonObjectConst>(),&duplicate));
  JsonDocument malformed;
  assert(deserializeJson(malformed,R"({"devices":[{"id":"A","name":"x"}]})")==DeserializationError::Ok);
  Catalog invalid{};assert(!parse(malformed.as<JsonObjectConst>(),&invalid));

  std::string many="{\"devices\":[";
  for(int i=0;i<12;++i){if(i)many+=",";many+=device("D"+std::to_string(i));}
  many+="]}";auto bounded=parsed(many);
  assert(bounded.count==MAX_DEVICES && bounded.truncated);
}

void policy_cases() {
  constexpr auto both="user-read-playback-state user-modify-playback-state";
  TransferPlan plan;plan.session(7,both);
  auto catalog=parsed("{\"devices\":["+device("ACTIVE",true)+","+device("TARGET")+","+device("PRIVATE",false,false,true)+","+device("LOCKED",false,true)+"]}");
  assert(plan.install(7,catalog,100));
  assert(plan.request(7,0,101)==0 && plan.request(7,3,101)==0);
  const auto ticket=plan.request(7,1,101);assert(ticket);
  assert(plan.request(7,2,101)==0);
  TransferRequest req{};assert(plan.take(7,102,&req));
  assert(std::string(req.device_id.data())=="TARGET" && plan.current(7,req,103));
  assert(plan.finish(req,204,true) && plan.outcome()==Outcome::CONFIRMED);
  assert(!plan.finish(req,204,true));

  plan.install(7,catalog,200);plan.transfer_action(7,true,false);
  assert(plan.request(7,1,201)==0);
  plan.transfer_action(7,true,true);
  assert(plan.request(7,2,202)); // private-session does not imply restricted
  assert(plan.take(7,203,&req));assert(plan.finish(req,403,true));
  assert(plan.outcome()==Outcome::FORBIDDEN);

  plan.install(7,catalog,300);assert(plan.request(7,1,301));assert(plan.take(7,302,&req));
  auto changed=catalog;changed.devices[1].restricted=true;changed.devices[1].selectable=false;
  assert(plan.install(7,changed,303));assert(!plan.current(7,req,304));
  assert(!plan.finish(req,204,true));

  plan.session(8,both);assert(plan.install(8,catalog,UINT32_MAX-5));
  assert(plan.request(8,1,UINT32_MAX-4));assert(plan.take(8,UINT32_MAX-3,&req));
  assert(plan.current(8,req,1));assert(plan.finish(req,429,true));
  assert(plan.outcome()==Outcome::RATE_LIMIT);

  TransferPlan stale;stale.session(1,both);assert(stale.install(1,catalog,10));
  assert(stale.request(1,1,10+CATALOG_TTL_MS)==0);
  TransferPlan read_only;read_only.session(1,"user-read-playback-state");
  assert(read_only.install(1,catalog,10) && read_only.request(1,1,11)==0);
  TransferPlan modify_only;modify_only.session(1,"user-modify-playback-state");
  assert(!modify_only.install(1,catalog,10));
}

void stress_cases() {
  constexpr auto both="user-read-playback-state user-modify-playback-state";
  const auto catalog=parsed("{\"devices\":["+device("TARGET")+"]}");
  for(uint32_t i=1;i<=10000;++i){
    TransferPlan plan;plan.session(i,both);assert(plan.install(i,catalog,i*17U));
    const uint32_t now=i*17U+1;assert(plan.request(i,0,now));
    TransferRequest req{};assert(plan.take(i,now+1,&req));
    assert(plan.current(i,req,now+2));assert(plan.finish(req,204,true));
    assert(plan.outcome()==Outcome::CONFIRMED);
  }
}

int main(){parser_cases();policy_cases();stress_cases();std::cout<<"SPOTIFY_DEVICE_TRANSFER_POLICY_NATIVE=PASS parser nullable duplicate bounds ttl generation stale restricted private action one_shot no_replay wrap scopes stress10000 no_network\n";}
