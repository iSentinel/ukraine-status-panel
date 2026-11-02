#pragma once
#include <ArduinoJson.h>
#include <cstdint>
#include <string_view>

namespace usp_spotify_controls {
struct ActionMask {uint8_t allowed{0};bool known{false};};

/** Presence and null are different: an explicit malformed restriction denies controls. */
inline bool contains(JsonObjectConst object,const char *name) {
  for(JsonPairConst entry:object)
    if(std::string_view(entry.key().c_str())==name)return true;
  return false;
}

inline bool restriction(JsonObjectConst object,const char *name,
                        bool omitted_allowed,bool *allowed) {
  if(!contains(object,name)) {*allowed=omitted_allowed;return true;}
  const JsonVariantConst value=object[name];
  if(!value.is<bool>())return false;
  *allowed=!value.as<bool>();
  return true;
}

/** Nested restrictions default to allowed; flat omissions remain unavailable. */
inline ActionMask action_mask(JsonVariantConst value) {
  if(!value.is<JsonObjectConst>())return {};
  JsonObjectConst actions=value.as<JsonObjectConst>();
  const bool supplied=contains(actions,"disallows");
  const bool nested=actions["disallows"].is<JsonObjectConst>();
  if(supplied && !nested)return {};
  const JsonObjectConst restrictions=nested?
      actions["disallows"].as<JsonObjectConst>():actions;
  constexpr const char *names[]={"resuming","pausing","skipping_next","skipping_prev"};
  ActionMask result{};
  for(unsigned i=0;i<4;++i) {
    bool allowed=false,flat=true;
    if(!restriction(restrictions,names[i],nested,&allowed))return {};
    if(nested && !restriction(actions,names[i],true,&flat))return {};
    if(allowed && flat)result.allowed|=uint8_t(1U<<i);
    result.known=result.known || nested || contains(restrictions,names[i]);
  }
  return result;
}
}  // namespace usp_spotify_controls
