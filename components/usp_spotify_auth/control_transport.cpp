#include "control_transport.h"
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
#include <cstring>

namespace esphome::usp_spotify_auth {
namespace {
using usp_spotify_controls::Command;
using usp_spotify_controls::Request;
using usp_spotify_controls::Response;
constexpr uint32_t DEADLINE_MS = 8000;
constexpr size_t MAX_REPLY_BYTES = 4096;
struct Target {
  const char *url;
  decltype(esp_http_client_config_t{}.method) method;
};

/** Only the user's active Spotify device is addressed; no query or arbitrary URL. */
Target target(Command command) {
  switch (command) {
    case Command::PLAY: return {"https://api.spotify.com/v1/me/player/play",HTTP_METHOD_PUT};
    case Command::PAUSE: return {"https://api.spotify.com/v1/me/player/pause",HTTP_METHOD_PUT};
    case Command::NEXT: return {"https://api.spotify.com/v1/me/player/next",HTTP_METHOD_POST};
    case Command::PREVIOUS: return {"https://api.spotify.com/v1/me/player/previous",HTTP_METHOD_POST};
  }
  return {nullptr,HTTP_METHOD_POST};
}

/** A queued tap expires before dispatch; in-flight results are never replayed. */
bool eligible(const Request &request, ControlCurrent current, void *context) {
  if (!request.ticket || !request.session || !target(request.command).url ||
      !current || !current(context,request)) return false;
  const int32_t remaining = static_cast<int32_t>(request.expires_at - millis());
  return remaining > 0 && remaining <= int32_t(usp_spotify_controls::QUEUE_TTL_MS);
}

/** Preserve the longest valid cooldown; excessive seconds fail closed. */
void retry_header(const char *key, const char *value, Response *result) {
  if (!key || !value) return;
  if (strnlen(key,12) != 11 || strnlen(value,65) == 65) return;
  constexpr char expected[] = "retry-after";
  for (size_t i=0;i<11;++i) {
    char ch=key[i];if(ch>='A' && ch<='Z')ch=char(ch-'A'+'a');
    if(ch!=expected[i])return;
  }
  size_t first=0,last=std::strlen(value);
  while(first<last && (value[first]==' ' || value[first]=='\t'))++first;
  while(last>first && (value[last-1]==' ' || value[last-1]=='\t'))--last;
  if(first==last)return;
  uint32_t seconds=0;
  for(size_t i=first;i<last;++i) {
    if(value[i]<'0' || value[i]>'9')return;
    seconds=std::min<uint32_t>(seconds*10U+uint32_t(value[i]-'0'),86401U);
  }
  if(seconds>86400U) {result->retry_excessive=true;return;}
  result->retry_known=true;
  result->retry_seconds=std::max(result->retry_seconds,std::max<uint32_t>(1,seconds));
}
struct Collector {
  uint32_t started;
  size_t bytes{0};
  bool rejected{false};
  Response response{};
};

/** Error bodies are bounded and discarded, never logged or copied to the UI. */
esp_err_t collect(esp_http_client_event_t *event) {
  auto *state=static_cast<Collector *>(event->user_data);
  if(!state)return ESP_FAIL;
  if(event->event_id==HTTP_EVENT_ON_HEADER)
    retry_header(event->header_key,event->header_value,&state->response);
  if(uint32_t(millis()-state->started)>=DEADLINE_MS) {
    state->rejected=true;return ESP_FAIL;
  }
  if(event->event_id!=HTTP_EVENT_ON_DATA)return ESP_OK;
  if(event->data_len<0 || (event->data_len && !event->data) ||
      state->bytes>MAX_REPLY_BYTES || size_t(event->data_len)>MAX_REPLY_BYTES-state->bytes) {
    state->rejected=true;return ESP_FAIL;
  }
  state->bytes+=size_t(event->data_len);
  return ESP_OK;
}

esp_http_client_config_t configuration(Target endpoint, Collector *collector) {
  esp_http_client_config_t config{};
  config.url=endpoint.url;config.method=endpoint.method;
  config.transport_type=HTTP_TRANSPORT_OVER_SSL;
  config.timeout_ms=5000;config.disable_auto_redirect=true;
  config.max_authorization_retries=-1;config.keep_alive_enable=false;
  config.skip_cert_common_name_check=false;
  config.buffer_size=1024;config.buffer_size_tx=1024;
  config.event_handler=collect;config.user_data=collector;
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
  config.crt_bundle_attach=esp_crt_bundle_attach;
#endif
  return config;
}

/** A fresh client sends an explicitly empty body and only the RAM bearer. */
bool prepare(esp_http_client_handle_t client, const char *authorization) {
  return esp_http_client_set_header(client,"Authorization",authorization)==ESP_OK &&
      esp_http_client_set_header(client,"Accept","application/json")==ESP_OK &&
      esp_http_client_set_header(client,"Accept-Encoding","identity")==ESP_OK &&
      esp_http_client_set_header(client,"Content-Length","0")==ESP_OK;
}

struct PrivateHeader {
  std::array<char,usp_spotify_tokens::MAX_TOKEN+8> value{};
  ~PrivateHeader() {usp_spotify_pkce::zeroize(value.data(),value.size());}
  void set(std::string_view token) {
    std::memcpy(value.data(),"Bearer ",7);
    std::memcpy(value.data()+7,token.data(),token.size());
  }
};

/** A command is attempted at most once; 204 is confirmed only after a complete reply. */
Response dispatch(esp_http_client_handle_t client, Collector &collector) {
  collector.response.attempted=true;
  const auto error=esp_http_client_perform(client);
  collector.response.status=esp_http_client_get_status_code(client);
  collector.response.complete=error==ESP_OK && !collector.rejected &&
      uint32_t(millis()-collector.started)<DEADLINE_MS &&
      esp_http_client_is_complete_data_received(client) &&
      (collector.response.status!=204 || collector.bytes==0);
  return collector.response;
}
}  // namespace

bool IdfControlTransport::available() {
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
  return true;
#else
  return false;
#endif
}

/** The caller owns the single flight and applies provider cooldown even after logout. */
Response IdfControlTransport::perform(const Request &request, std::string_view token,
                                     ControlCurrent current, void *context) {
  if(!available() || !network::is_connected() || !eligible(request,current,context) ||
      !usp_spotify_tokens::bearer_value(token))return {};
  PrivateHeader authorization;authorization.set(token);
  usp_network::HttpsTransaction admission("spotify",usp_network::HttpsPriority::BACKGROUND,250);
  if(!admission.acquired() || !eligible(request,current,context))return {};
  Collector collector{millis(),0,false,{}};
  auto config=configuration(target(request.command),&collector);
  const auto client=esp_http_client_init(&config);
  if(!client)return {};
  Response result{};
  if(prepare(client,authorization.value.data()) && eligible(request,current,context))
    result=dispatch(client,collector);
  esp_http_client_cleanup(client);
  return result;
}
}  // namespace esphome::usp_spotify_auth
