#include "playback_model.h"
#include "stage4_playback_facts.h"
#include <ArduinoJson.h>
#include <cassert>
#include <iostream>
#include <string>
using namespace usp_spotify_playback;
static std::string base(const std::string &extra,const std::string &actions) {
 return std::string(R"({"is_playing":true,"progress_ms":1000,"device":{"is_active":true,"is_private_session":false,"is_restricted":false,"supports_volume":true,"name":"Office","type":"speaker","volume_percent":55},"item":{"type":"track","name":"Song","duration_ms":200000,"album":{"name":"Album","images":[]},"artists":[{"name":"Artist"}]},)")+extra+"\"actions\":"+actions+"}";
}
static PlaybackSnapshot parse(const std::string &text) {JsonDocument d;assert(deserializeJson(d,text)==DeserializationError::Ok);PlaybackSnapshot s{};assert(parse_playback(d.as<JsonObjectConst>(),&s));return s;}
int main(){
 const std::string actions=R"({"disallows":{"resuming":false,"pausing":false,"skipping_next":false,"skipping_prev":false,"seeking":false,"toggling_shuffle":false,"toggling_repeat_context":true,"toggling_repeat_track":false,"transferring_playback":false}})";
 auto s=parse(base(R"("shuffle_state":true,"repeat_state":"track",)",actions));
 assert(s.valid&&s.mode_state_known&&s.shuffle_state&&s.repeat_state==RepeatState::TRACK);
 assert(s.seek.known&&s.seek.allowed&&s.shuffle.known&&s.shuffle.allowed);
 assert(s.repeat_context.known&&!s.repeat_context.allowed&&s.repeat_track.allowed&&s.transfer.allowed);
 auto vf=usp_spotify_stage4_binding::value_facts(s,1000);assert(vf.seek.known&&vf.seek.allowed&&vf.supports_volume);
 auto mf=usp_spotify_stage4_binding::mode_facts(s,1000);assert(mf.valid&&mf.shuffle&&mf.repeat==usp_spotify_modes::Repeat::TRACK&&!mf.repeat_context.allowed);
 auto qf=usp_spotify_stage4_binding::queue_facts(s,1000);assert(qf.valid&&qf.active&&!qf.restricted);
 auto old=parse(base("",R"({"disallows":{"resuming":false,"pausing":false,"skipping_next":false,"skipping_prev":false}})"));
 assert(old.valid&&!old.mode_state_known&&old.seek.known&&old.seek.allowed&&old.transfer.known&&old.transfer.allowed);
 JsonDocument bad;auto text=base(R"("shuffle_state":true,"repeat_state":"bogus",)",actions);assert(deserializeJson(bad,text)==DeserializationError::Ok);PlaybackSnapshot rejected{};assert(!parse_playback(bad.as<JsonObjectConst>(),&rejected));
 std::cout<<"SPOTIFY_STAGE4_PLAYBACK_EXTENDED=PASS bounded primitives old_fixture_compatible malformed_reject facts_projection no_network\n";
}
