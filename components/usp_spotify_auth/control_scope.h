#pragma once
#include <string_view>

namespace usp_spotify_controls {
/** Check whole returned scope names; malformed input never grants a permission. */
inline bool has_scope(std::string_view granted, std::string_view required) {
  if(granted.empty() || granted.size()>512 || required.empty())return false;
  bool found=false;
  size_t start=0;
  for(size_t i=0;i<=granted.size();++i) {
    if(i==granted.size() || granted[i]==' ') {
      if(i>start && granted.substr(start,i-start)==required)found=true;
      start=i+1;
    } else {
      const unsigned char ch=granted[i];
      if(ch<0x21 || ch>0x7e || ch=='"' || ch=='\\')return false;
    }
  }
  return found;
}
}  // namespace usp_spotify_controls
