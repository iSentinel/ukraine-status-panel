#pragma once
#include <cassert>
#include <cstring>
namespace esphome::usp_network {
enum class HttpsPriority {BACKGROUND,NORMAL,CRITICAL};
inline const char *host_expected_owner="spotify";
inline bool host_admitted=true,host_allow_artwork=false;
inline int host_active=0;
class HttpsTransaction {
 public:
  HttpsTransaction(const char *owner,HttpsPriority priority,unsigned wait) {
    assert((std::strcmp(owner,host_expected_owner)==0 || (host_allow_artwork && std::strcmp(owner,"spotify-artwork")==0)) && priority==HttpsPriority::BACKGROUND && wait==250);
    ready_=host_admitted;if(ready_)++host_active;
  }
  ~HttpsTransaction() {if(ready_)--host_active;}
  bool acquired()const{return ready_;}
 private:bool ready_;
};
}
