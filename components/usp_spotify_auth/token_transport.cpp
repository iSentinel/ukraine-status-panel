#include "token_transport.h"
#include "esphome/components/network/util.h"
#include "esphome/components/usp_weather/network_gate.h"
#include "esphome/core/hal.h"
#include "esp_http_client.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif
#include <cstring>

namespace esphome::usp_spotify_auth {
namespace {
using usp_spotify_tokens::HttpResult;
constexpr char ENDPOINT[]="https://accounts.spotify.com/api/token";

/** Delay beyond one day disables automatic retry instead of shortening it. */
void retry_header(const char *key,const char *value,HttpResult *result) {
  if (!key || !value) return;
  size_t key_length=0;
  while (key_length<=11 && key[key_length]) ++key_length;
  if (key_length!=11) return;
  constexpr char expected[]="retry-after";
  for(size_t i=0;i<11;++i) {
    char ch=key[i];if(ch>='A' && ch<='Z')ch=char(ch-'A'+'a');
    if(ch!=expected[i])return;
  }
  const size_t length=strnlen(value,65);
  if(length==65)return;
  size_t first=0,last=length;
  while(first<last && (value[first]==' ' || value[first]=='\t'))++first;
  while(last>first && (value[last-1]==' ' || value[last-1]=='\t'))--last;
  if(first==last)return;
  uint32_t seconds=0;
  for(size_t i=first;i<last;++i) {
    if(value[i]<'0' || value[i]>'9')return;
    seconds=std::min<uint32_t>(seconds*10U+uint32_t(value[i]-'0'),86401U);
  }
  if(seconds>86400U) {result->retry_excess=true;return;}
  result->retry_valid=true;
  result->retry_ms=std::max(result->retry_ms,std::max<uint32_t>(1000U,seconds*1000U));
}

struct Collector {
  char *body;
  size_t capacity,size{0};
  uint32_t started;
  bool overflow{false};
  HttpResult result{};
};

/** Copy only bounded data; partial, overflowing or late replies stay unconfirmed. */
esp_err_t collect(esp_http_client_event_t *event) {
  auto *body=static_cast<Collector *>(event->user_data);
  if (!body) return ESP_FAIL;
  if(event->event_id!=HTTP_EVENT_ON_HEADER && event->event_id!=HTTP_EVENT_ON_DATA)return ESP_OK;
  if(uint32_t(millis()-body->started)>=8000U) {body->overflow=true;return ESP_FAIL;}
  if(event->event_id==HTTP_EVENT_ON_HEADER) {
    retry_header(event->header_key,event->header_value,&body->result);return ESP_OK;
  }
  if(event->data_len<0 || (event->data_len && !event->data) ||
      body->size>body->capacity || size_t(event->data_len)>body->capacity-body->size) {
    body->overflow=true;return ESP_FAIL;
  }
  if(event->data_len)std::memcpy(body->body+body->size,event->data,size_t(event->data_len));
  body->size+=size_t(event->data_len);return ESP_OK;
}

esp_http_client_config_t configuration(Collector *body) {
  esp_http_client_config_t config{};
  config.url=ENDPOINT;config.method=HTTP_METHOD_POST;
  config.transport_type=HTTP_TRANSPORT_OVER_SSL;
  config.timeout_ms=5000;config.disable_auto_redirect=true;
  config.max_authorization_retries=-1;
  config.keep_alive_enable=false;config.skip_cert_common_name_check=false;
  config.buffer_size=1024;config.buffer_size_tx=1024;
  config.event_handler=collect;config.user_data=body;
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
  config.crt_bundle_attach=esp_crt_bundle_attach;
#endif
  return config;
}

bool prepare(esp_http_client_handle_t client,const char *body,size_t size) {
  return esp_http_client_set_header(client,"Content-Type","application/x-www-form-urlencoded")==ESP_OK &&
      esp_http_client_set_header(client,"Accept","application/json")==ESP_OK &&
      esp_http_client_set_header(client,"Accept-Encoding","identity")==ESP_OK &&
      esp_http_client_set_post_field(client,body,int(size))==ESP_OK;
}
}  // namespace

bool IdfTokenTransport::available() {
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
  return true;
#else
  return false;
#endif
}

HttpResult IdfTokenTransport::perform(const char *data,size_t size,char *output,size_t capacity) {
  if(!data || !size || size>usp_spotify_tokens::MAX_FORM || !output || !capacity ||
      capacity>usp_spotify_tokens::MAX_BODY || !this->available() || !network::is_connected())return {};
  usp_network::HttpsTransaction admission("spotify",usp_network::HttpsPriority::BACKGROUND,250);
  if(!admission.acquired())return {};
  Collector body{output,capacity,0,millis(),false,{}};
  auto config=configuration(&body);
  const auto client=esp_http_client_init(&config);
  if(!client)return {};
  if(prepare(client,data,size)) {
    body.result.attempted=true;
    const auto error=esp_http_client_perform(client);
    body.result.status=esp_http_client_get_status_code(client);
    body.result.complete=error==ESP_OK && !body.overflow && esp_http_client_is_complete_data_received(client);
  }
  body.result.size=body.size;
  esp_http_client_cleanup(client);
  return body.result;
}
}  // namespace esphome::usp_spotify_auth
