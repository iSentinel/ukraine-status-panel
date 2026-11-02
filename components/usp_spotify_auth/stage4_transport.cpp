#include "stage4_transport.h"
#include "usp_spotify_tokens.h"
#include "esphome/components/network/util.h"
#include "esphome/components/usp_weather/network_gate.h"
#include "esphome/core/hal.h"
#include "esp_http_client.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace esphome::usp_spotify_auth { namespace {
constexpr char DEVICES_URL[]="https://api.spotify.com/v1/me/player/devices";
constexpr char QUEUE_URL[]="https://api.spotify.com/v1/me/player/queue";
constexpr char TRANSFER_URL[]="https://api.spotify.com/v1/me/player";
constexpr uint32_t DEADLINE_MS=8000;constexpr size_t MAX_ERROR=4096,MAX_URL=256;
struct Collector {char *body{nullptr};size_t capacity{0},size{0};uint32_t started{0};bool rejected{false};Stage4HttpResult result{};};
void retry_header(const char *key,const char *value,Stage4HttpResult *result){
  if(!key||!value||!result||strnlen(key,12)!=11||strnlen(value,65)==65)return;
  constexpr char expected[]="retry-after";for(size_t i=0;i<11;++i){char ch=key[i];if(ch>='A'&&ch<='Z')ch=char(ch-'A'+'a');if(ch!=expected[i])return;}
  size_t first=0,last=std::strlen(value);while(first<last&&(value[first]==' '||value[first]=='\t'))++first;while(last>first&&(value[last-1]==' '||value[last-1]=='\t'))--last;if(first==last)return;
  uint32_t seconds=0;for(size_t i=first;i<last;++i){if(value[i]<'0'||value[i]>'9')return;seconds=std::min<uint32_t>(seconds*10U+uint32_t(value[i]-'0'),86401U);}
  if(seconds>86400U){result->retry_excessive=true;return;}result->retry_known=true;result->retry_seconds=std::max(result->retry_seconds,std::max<uint32_t>(1,seconds));
}
esp_err_t collect(esp_http_client_event_t *e){auto *s=static_cast<Collector *>(e->user_data);if(!s)return ESP_FAIL;if(e->event_id==HTTP_EVENT_ON_HEADER)retry_header(e->header_key,e->header_value,&s->result);if(uint32_t(millis()-s->started)>=DEADLINE_MS){s->rejected=true;return ESP_FAIL;}if(e->event_id!=HTTP_EVENT_ON_DATA)return ESP_OK;if(e->data_len<0||(e->data_len&&!e->data)||s->size>s->capacity||size_t(e->data_len)>s->capacity-s->size){s->rejected=true;return ESP_FAIL;}if(e->data_len&&s->body)std::memcpy(s->body+s->size,e->data,size_t(e->data_len));s->size+=size_t(e->data_len);return ESP_OK;}
struct PrivateHeader{std::array<char,usp_spotify_tokens::MAX_TOKEN+8> value{};~PrivateHeader(){usp_spotify_pkce::zeroize(value.data(),value.size());}bool set(std::string_view token){if(!usp_spotify_tokens::bearer_value(token))return false;std::memcpy(value.data(),"Bearer ",7);std::memcpy(value.data()+7,token.data(),token.size());return true;}};
esp_http_client_config_t config(const char *url,decltype(esp_http_client_config_t{}.method) method,Collector *s){esp_http_client_config_t c{};c.url=url;c.method=method;c.transport_type=HTTP_TRANSPORT_OVER_SSL;c.timeout_ms=5000;c.disable_auto_redirect=true;c.max_authorization_retries=-1;c.keep_alive_enable=false;c.skip_cert_common_name_check=false;c.buffer_size=2048;c.buffer_size_tx=1024;c.event_handler=collect;c.user_data=s;
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
 c.crt_bundle_attach=esp_crt_bundle_attach;
#endif
 return c;}
bool prepare(esp_http_client_handle_t client,const char *auth){return esp_http_client_set_header(client,"Authorization",auth)==ESP_OK&&esp_http_client_set_header(client,"Accept","application/json")==ESP_OK&&esp_http_client_set_header(client,"Accept-Encoding","identity")==ESP_OK;}
Stage4HttpResult dispatch(esp_http_client_handle_t client,Collector *s){s->result.attempted=true;const auto err=esp_http_client_perform(client);s->result.status=esp_http_client_get_status_code(client);s->result.complete=err==ESP_OK&&!s->rejected&&uint32_t(millis()-s->started)<DEADLINE_MS&&esp_http_client_is_complete_data_received(client)&&(s->result.status!=204||s->size==0);s->result.size=s->size;return s->result;}
template<class Request,class Current>bool eligible(const Request &request,uint32_t ttl,Current current,void *ctx){if(!current||!current(ctx,request))return false;const int32_t left=int32_t(request.expires_at-millis());return left>0&&left<=int32_t(ttl);}
bool transfer_body(const usp_spotify_devices::TransferRequest&r,std::array<char,192>*out,size_t*size){if(!out||!size||!r.device_id[0])return false;for(unsigned char ch:std::string_view(r.device_id.data()))if(ch<0x21||ch>0x7e||ch=='"'||ch=='\\')return false;int n=std::snprintf(out->data(),out->size(),"{\"device_ids\":[\"%s\"]}",r.device_id.data());if(n<=0||size_t(n)>=out->size())return false;*size=size_t(n);return true;}
bool value_url(const usp_spotify_value_controls::Request&r,std::array<char,MAX_URL>*out){if(!out)return false;int n=-1;if(r.kind==usp_spotify_value_controls::Kind::VOLUME&&r.value<=100)n=std::snprintf(out->data(),out->size(),"https://api.spotify.com/v1/me/player/volume?volume_percent=%u",unsigned(r.value));else if(r.kind==usp_spotify_value_controls::Kind::SEEK&&r.value)n=std::snprintf(out->data(),out->size(),"https://api.spotify.com/v1/me/player/seek?position_ms=%u",unsigned(r.value));return n>0&&size_t(n)<out->size();}
bool mode_url(const usp_spotify_modes::Request&r,std::array<char,MAX_URL>*out){if(!out)return false;int n=-1;if(r.kind==usp_spotify_modes::Kind::SHUFFLE)n=std::snprintf(out->data(),out->size(),"https://api.spotify.com/v1/me/player/shuffle?state=%s",r.shuffle?"true":"false");else{const char *state=r.repeat==usp_spotify_modes::Repeat::OFF?"off":r.repeat==usp_spotify_modes::Repeat::CONTEXT?"context":"track";n=std::snprintf(out->data(),out->size(),"https://api.spotify.com/v1/me/player/repeat?state=%s",state);}return n>0&&size_t(n)<out->size();}
bool queue_url(const usp_spotify_queue::AddRequest&r,std::array<char,MAX_URL>*out){if(!out||!usp_spotify_queue::spotify_uri(r.uri.data()))return false;const std::string_view uri(r.uri.data()),track="spotify:track:",episode="spotify:episode:";const bool is_track=uri.rfind(track,0)==0;const auto prefix=is_track?track:episode;const auto id=uri.substr(prefix.size());int n=std::snprintf(out->data(),out->size(),"https://api.spotify.com/v1/me/player/queue?uri=spotify%%3A%s%%3A%.*s",is_track?"track":"episode",int(id.size()),id.data());return n>0&&size_t(n)<out->size();}
Stage4HttpResult get(const char *url,std::string_view token,char *body,size_t capacity,size_t maximum){if(!IdfStage4Transport::available()||!network::is_connected()||!body||!capacity||capacity>maximum)return {};PrivateHeader auth;if(!auth.set(token))return {};usp_network::HttpsTransaction admission("spotify",usp_network::HttpsPriority::BACKGROUND,250);if(!admission.acquired())return {};Collector s{body,capacity,0,millis(),false,{}};auto c=config(url,HTTP_METHOD_GET,&s);auto client=esp_http_client_init(&c);if(!client)return {};Stage4HttpResult result{};if(prepare(client,auth.value.data()))result=dispatch(client,&s);esp_http_client_cleanup(client);return result;}
template<class Request,class Current>Stage4HttpResult empty_command(const Request&r,const char *url,decltype(esp_http_client_config_t{}.method) method,uint32_t ttl,std::string_view token,Current current,void*ctx){if(!IdfStage4Transport::available()||!network::is_connected()||!url||!eligible(r,ttl,current,ctx))return {};PrivateHeader auth;if(!auth.set(token))return {};usp_network::HttpsTransaction admission("spotify",usp_network::HttpsPriority::BACKGROUND,250);if(!admission.acquired()||!eligible(r,ttl,current,ctx))return {};Collector s{nullptr,MAX_ERROR,0,millis(),false,{}};auto c=config(url,method,&s);auto client=esp_http_client_init(&c);if(!client)return {};Stage4HttpResult result{};if(prepare(client,auth.value.data())&&esp_http_client_set_header(client,"Content-Length","0")==ESP_OK&&eligible(r,ttl,current,ctx))result=dispatch(client,&s);esp_http_client_cleanup(client);return result;}
} // namespace
bool IdfStage4Transport::available(){
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
 return true;
#else
 return false;
#endif
}
Stage4HttpResult IdfStage4Transport::devices(std::string_view t,char*b,size_t c){return get(DEVICES_URL,t,b,c,STAGE4_DEVICE_BODY);}
Stage4HttpResult IdfStage4Transport::queue(std::string_view t,char*b,size_t c){return get(QUEUE_URL,t,b,c,STAGE4_QUEUE_BODY);}
Stage4HttpResult IdfStage4Transport::transfer(const usp_spotify_devices::TransferRequest&r,std::string_view token,TransferCurrent current,void*ctx){if(!available()||!network::is_connected()||!eligible(r,usp_spotify_devices::TRANSFER_TTL_MS,current,ctx))return {};PrivateHeader auth;if(!auth.set(token))return {};std::array<char,192> body{};size_t size=0;if(!transfer_body(r,&body,&size))return {};usp_network::HttpsTransaction admission("spotify",usp_network::HttpsPriority::BACKGROUND,250);if(!admission.acquired()||!eligible(r,usp_spotify_devices::TRANSFER_TTL_MS,current,ctx))return {};Collector s{nullptr,MAX_ERROR,0,millis(),false,{}};auto c=config(TRANSFER_URL,HTTP_METHOD_PUT,&s);auto client=esp_http_client_init(&c);if(!client)return {};Stage4HttpResult result{};if(prepare(client,auth.value.data())&&esp_http_client_set_header(client,"Content-Type","application/json")==ESP_OK&&esp_http_client_set_post_field(client,body.data(),int(size))==ESP_OK&&eligible(r,usp_spotify_devices::TRANSFER_TTL_MS,current,ctx))result=dispatch(client,&s);esp_http_client_cleanup(client);usp_spotify_pkce::zeroize(body.data(),body.size());return result;}
Stage4HttpResult IdfStage4Transport::value(const usp_spotify_value_controls::Request&r,std::string_view token,ValueCurrent current,void*ctx){std::array<char,MAX_URL> url{};if(!value_url(r,&url))return {};auto result=empty_command(r,url.data(),HTTP_METHOD_PUT,usp_spotify_value_controls::REQUEST_TTL_MS,token,current,ctx);usp_spotify_pkce::zeroize(url.data(),url.size());return result;}
Stage4HttpResult IdfStage4Transport::mode(const usp_spotify_modes::Request&r,std::string_view token,ModeCurrent current,void*ctx){std::array<char,MAX_URL> url{};if(!mode_url(r,&url))return {};auto result=empty_command(r,url.data(),HTTP_METHOD_PUT,usp_spotify_modes::REQUEST_TTL_MS,token,current,ctx);usp_spotify_pkce::zeroize(url.data(),url.size());return result;}
Stage4HttpResult IdfStage4Transport::queue_add(const usp_spotify_queue::AddRequest&r,std::string_view token,QueueCurrent current,void*ctx){std::array<char,MAX_URL> url{};if(!queue_url(r,&url))return {};auto result=empty_command(r,url.data(),HTTP_METHOD_POST,usp_spotify_queue::REQUEST_TTL_MS,token,current,ctx);usp_spotify_pkce::zeroize(url.data(),url.size());return result;}
}  // namespace esphome::usp_spotify_auth
