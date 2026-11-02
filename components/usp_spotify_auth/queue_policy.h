#pragma once
#include "control_scope.h"
#include <ArduinoJson.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string_view>

namespace usp_spotify_queue {
constexpr size_t MAX_ITEMS=12;
constexpr size_t MAX_URI=128;
constexpr size_t MAX_NAME=160;
constexpr size_t MAX_SUBTITLE=192;
constexpr uint32_t QUEUE_TTL_MS=10000;
constexpr uint32_t REQUEST_TTL_MS=3000;
enum class ItemType:uint8_t{NONE,TRACK,EPISODE};
enum class Phase:uint8_t{IDLE,QUEUED,IN_FLIGHT};
enum class Outcome:uint8_t{NONE,CONFIRMED,REAUTH,FORBIDDEN,RATE_LIMIT,RETRY};
struct Item{
  std::array<char,MAX_URI+1> uri{};
  std::array<char,MAX_NAME+1> name{};
  std::array<char,MAX_SUBTITLE+1> subtitle{};
  uint32_t duration_ms{0};ItemType type{ItemType::NONE};
  /** Clear in place, without constructing a second model on the task stack. */
  void clear() {
    uri.fill(0);name.fill(0);subtitle.fill(0);
    duration_ms=0;type=ItemType::NONE;
  }
};
struct Queue {
  Item current{};bool has_current{false};std::array<Item,MAX_ITEMS> items{};
  uint8_t count{0};bool valid{false},truncated{false};
  /** Keep every queue buffer in caller-owned RAM, including reset operations. */
  void clear() {
    current.clear();for(auto &item:items)item.clear();
    has_current=false;count=0;valid=false;truncated=false;
  }
};

inline bool spotify_uri(std::string_view uri,ItemType *type=nullptr){
  constexpr std::string_view track="spotify:track:",episode="spotify:episode:";
  ItemType found=ItemType::NONE;size_t at=0;
  if(uri.rfind(track,0)==0){found=ItemType::TRACK;at=track.size();}
  else if(uri.rfind(episode,0)==0){found=ItemType::EPISODE;at=episode.size();}
  else return false;
  const size_t id_len=uri.size()-at;if(!id_len||id_len>64||uri.size()>MAX_URI)return false;
  for(size_t i=at;i<uri.size();++i){const unsigned char ch=uri[i];if(!((ch>='0'&&ch<='9')||(ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')))return false;}
  if(type) *type=found;
  return true;
}

template<size_t N> bool copy_ascii(std::array<char,N>&out,std::string_view value){out.fill(0);if(value.size()>=N)return false;for(unsigned char ch:value)if(ch<0x20||ch>0x7e)return false;std::memcpy(out.data(),value.data(),value.size());return true;}
inline size_t utf8_width(const unsigned char *text,size_t left){if(!left)return 0;const unsigned char first=text[0];if(first<0x20||first==0x7f)return 0;if(first<0x80)return 1;if(first>=0xC2&&first<=0xDF)return left>=2&&(text[1]&0xC0)==0x80?2:0;if(first>=0xE0&&first<=0xEF){if(left<3||(text[1]&0xC0)!=0x80||(text[2]&0xC0)!=0x80)return 0;if((first==0xE0&&text[1]<0xA0)||(first==0xED&&text[1]>=0xA0))return 0;return 3;}if(first>=0xF0&&first<=0xF4){if(left<4||(text[1]&0xC0)!=0x80||(text[2]&0xC0)!=0x80||(text[3]&0xC0)!=0x80)return 0;if((first==0xF0&&text[1]<0x90)||(first==0xF4&&text[1]>0x8F))return 0;return 4;}return 0;}
template<size_t N> bool copy_utf8(std::array<char,N>&out,std::string_view value){static_assert(N>0);out.fill(0);size_t in=0,at=0;while(in<value.size()){const auto*bytes=reinterpret_cast<const unsigned char*>(value.data()+in);const size_t width=utf8_width(bytes,value.size()-in);if(!width){out.fill(0);return false;}if(at+width>=N)break;std::memcpy(out.data()+at,value.data()+in,width);in+=width;at+=width;}out[at]=0;return true;}
inline bool text(JsonVariantConst v,std::string_view*out){if(!out||!v.is<const char*>())return false;const char*raw=v.as<const char*>();if(!raw)return false;*out=raw;return true;}
inline bool append(std::array<char,MAX_SUBTITLE+1>&out,std::string_view value){std::array<char,MAX_SUBTITLE+1> clean{};if(!copy_utf8(clean,value))return false;size_t used=std::strlen(out.data()),add=std::strlen(clean.data());size_t need=used+(used?2:0)+add;if(need>=out.size())return true;if(used){out[used++]=',';out[used++]=' ';}std::memcpy(out.data()+used,clean.data(),add);out[used+add]=0;return true;}
inline bool parse_item(JsonObjectConst object,Item*out){
  if(!out||object.isNull()) return false;
  Item next{};
  std::string_view type,uri,name;
  if(!text(object["type"],&type)||!text(object["uri"],&uri)||!text(object["name"],&name)||!spotify_uri(uri,&next.type)||!copy_ascii(next.uri,uri)||!copy_utf8(next.name,name))return false;
  if((type=="track")!=(next.type==ItemType::TRACK)||(type=="episode")!=(next.type==ItemType::EPISODE))return false;
  if(object["duration_ms"].is<uint32_t>())next.duration_ms=object["duration_ms"].as<uint32_t>();
  if(next.type==ItemType::TRACK){JsonArrayConst artists=object["artists"].as<JsonArrayConst>();if(artists.isNull()||artists.size()==0)return false;for(JsonObjectConst artist:artists){std::string_view artist_name;if(artist.isNull()||!text(artist["name"],&artist_name)||!append(next.subtitle,artist_name))return false;}}
  else {JsonObjectConst show=object["show"].as<JsonObjectConst>();std::string_view show_name;if(show.isNull()||!text(show["name"],&show_name)||!copy_utf8(next.subtitle,show_name))return false;}
  *out=next;return true;
}
/** Parse into caller-owned scratch. Partial data is never marked valid. */
inline bool parse_into(JsonObjectConst root,Queue*out){
  if(!out)return false;
  out->clear();
  if(root.isNull())return false;
  JsonVariantConst current=root["currently_playing"];
  if(!current.isNull()){
    if(!parse_item(current.as<JsonObjectConst>(),&out->current))return false;
    out->has_current=true;
  }
  JsonArrayConst queue=root["queue"].as<JsonArrayConst>();
  if(queue.isNull())return false;
  for(JsonObjectConst object:queue){
    Item item{};
    if(!parse_item(object,&item))return false;
    if(out->count<MAX_ITEMS)out->items[out->count++]=item;
    else out->truncated=true;
  }
  out->valid=true;return true;
}
/** Preserve the transactional public parser; firmware uses PSRAM parse_into. */
inline bool parse(JsonObjectConst root,Queue*out){
  if(!out)return false;
  Queue next{};
  if(!parse_into(root,&next))return false;
  *out=next;return true;
}

struct Facts{uint32_t updated_at{0};bool valid{false},active{false},restricted{true};};
struct AddRequest{uint64_t ticket{0};uint32_t generation{0},facts_revision{0},expires_at{0};std::array<char,MAX_URI+1> uri{};};
class AddPlan{
 public:
  void session(uint32_t generation,std::string_view scope){std::lock_guard<std::mutex>lock(mutex_);generation_=generation;modify_scope_=usp_spotify_controls::has_scope(scope,"user-modify-playback-state");facts_={};++facts_revision_;reset_();outcome_=Outcome::NONE;}
  void facts(uint32_t generation,const Facts&facts){std::lock_guard<std::mutex>lock(mutex_);if(generation!=generation_)return;facts_=facts;++facts_revision_;if(phase_==Phase::QUEUED)reset_();}
  uint64_t request(uint32_t generation,std::string_view uri,uint32_t now){std::unique_lock<std::mutex>lock(mutex_,std::try_to_lock);if(!lock.owns_lock()||phase_!=Phase::IDLE||!base_(generation,now)||!spotify_uri(uri))return 0;AddRequest r{};r.ticket=next_ticket_++;r.generation=generation;r.facts_revision=facts_revision_;r.expires_at=now+REQUEST_TTL_MS;copy_ascii(r.uri,uri);queued_=r;phase_=Phase::QUEUED;outcome_=Outcome::NONE;return r.ticket;}
  bool take(uint32_t generation,uint32_t now,AddRequest*out){std::lock_guard<std::mutex>lock(mutex_);if(!out||phase_!=Phase::QUEUED||!current_(generation,queued_,now)){if(phase_==Phase::QUEUED)reset_();return false;}loan_=queued_;queued_={};phase_=Phase::IN_FLIGHT;*out=loan_;return true;}
  bool current(uint32_t generation,const AddRequest&r,uint32_t now){std::lock_guard<std::mutex>lock(mutex_);return phase_==Phase::IN_FLIGHT&&same_(loan_,r)&&current_(generation,r,now);}
  bool finish(const AddRequest&r,int status,bool complete){std::lock_guard<std::mutex>lock(mutex_);if(phase_!=Phase::IN_FLIGHT||!same_(loan_,r))return false;outcome_=classify_(status,complete);reset_();return true;}
  Outcome outcome()const{return outcome_;}
 private:
  bool fresh_(uint32_t now)const{return facts_.valid&&uint32_t(now-facts_.updated_at)<QUEUE_TTL_MS;}
  bool base_(uint32_t generation,uint32_t now)const{return generation==generation_&&modify_scope_&&fresh_(now)&&facts_.active&&!facts_.restricted;}
  bool current_(uint32_t generation,const AddRequest&r,uint32_t now)const{return generation==generation_&&r.generation==generation_&&r.facts_revision==facts_revision_&&int32_t(r.expires_at-now)>0&&spotify_uri(r.uri.data())&&base_(generation,now);}
  static bool same_(const AddRequest&a,const AddRequest&b){return a.ticket==b.ticket&&a.generation==b.generation&&a.facts_revision==b.facts_revision&&a.expires_at==b.expires_at&&a.uri==b.uri;}
  static Outcome classify_(int status,bool complete){if(complete&&status==204)return Outcome::CONFIRMED;if(status==401)return Outcome::REAUTH;if(status==403)return Outcome::FORBIDDEN;if(status==429)return Outcome::RATE_LIMIT;return Outcome::RETRY;}
  void reset_(){queued_={};loan_={};phase_=Phase::IDLE;}
  mutable std::mutex mutex_;Facts facts_{};AddRequest queued_{},loan_{};uint64_t next_ticket_{1};uint32_t generation_{0},facts_revision_{0};Phase phase_{Phase::IDLE};Outcome outcome_{Outcome::NONE};bool modify_scope_{false};
};
static_assert(sizeof(Queue)<=8192,"Queue model must stay bounded");
static_assert(sizeof(AddPlan)<=512,"Queue add policy must stay bounded metadata");
} // namespace usp_spotify_queue
