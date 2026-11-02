#pragma once
#include "esphome/core/hal.h"
#include "esp_err.h"
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <cassert>
#include <functional>
enum {HTTP_METHOD_POST,HTTP_METHOD_GET,HTTP_METHOD_PUT,HTTP_TRANSPORT_OVER_SSL,HTTP_EVENT_ON_HEADER,HTTP_EVENT_ON_DATA,HTTP_EVENT_REDIRECT};
struct esp_http_client_event_t {
  int event_id;void *user_data;char *header_key=nullptr,*header_value=nullptr;
  void *data=nullptr;int data_len=0;
};
struct esp_http_client_config_t {
  const char *url=nullptr;
  int method=0,transport_type=0,timeout_ms=0,max_authorization_retries=0,buffer_size=0,buffer_size_tx=0;
  bool disable_auto_redirect=false,keep_alive_enable=true,skip_cert_common_name_check=true;
  int (*event_handler)(esp_http_client_event_t *)=nullptr;
  int (*crt_bundle_attach)(void *)=nullptr;
  void *user_data=nullptr;
};
struct HostClient {esp_http_client_config_t config;int selected_status=200;std::map<std::string,std::string> request_headers{};};
using esp_http_client_handle_t=HostClient *;
namespace http_fixture {
struct RequestRecord {std::string url;std::map<std::string,std::string> headers;int method;bool tls_bundle,redirect_disabled;};
inline std::vector<RequestRecord> requests;
inline std::atomic<int> artwork_calls{0},control_calls{0};
inline std::string control_response;
inline int control_status=204;
inline std::vector<std::pair<std::string,std::string>> control_headers;
inline std::function<void()> before_control;
inline std::string artwork_response;
inline int artwork_status=200;
inline std::vector<std::pair<std::string,std::string>> artwork_headers;

inline esp_http_client_config_t config;
inline std::map<std::string,std::string> headers;
inline std::vector<std::pair<std::string,std::string>> response_headers;
inline std::string posted,response;
inline std::atomic<int> calls{0},cleanups{0},token_calls{0},player_calls{0};
inline int status=200,player_status=204;
inline bool split_spotify=false;
inline std::string player_response;
inline std::function<void()> before_perform;
inline bool fail_init=false,fail_prepare=false,fail_perform=false,complete=true,null_data=false,negative_data=false,slow=false;
inline void reset() {
  control_calls=0;control_response.clear();control_status=204;control_headers.clear();before_control={};
  requests.clear();artwork_calls=0;artwork_response.clear();artwork_status=200;artwork_headers.clear();
  before_perform={};response_headers.clear();headers.clear();posted.clear();response="{}";player_response.clear();calls=cleanups=token_calls=player_calls=0;status=200;player_status=204;split_spotify=false;
  fail_init=fail_prepare=fail_perform=null_data=negative_data=slow=false;complete=true;
  esphome::transport_now=100;
}
}
inline auto esp_http_client_init(const esp_http_client_config_t *config) {
  http_fixture::config=*config;
  return http_fixture::fail_init?nullptr:new HostClient{*config,200,{}};
}
inline int esp_http_client_set_header(HostClient *client,const char *key,const char *value) {
  client->request_headers[key]=value;http_fixture::headers[key]=value;return http_fixture::fail_prepare?ESP_FAIL:ESP_OK;
}
inline int esp_http_client_set_post_field(HostClient *,const char *body,int length) {
  http_fixture::posted.assign(body,size_t(length));return ESP_OK;
}
inline int esp_http_client_perform(HostClient *client) {
  using namespace http_fixture;
  ++calls;
  const bool player=client->config.url && std::string(client->config.url).find("api.spotify.com/v1/me/player")!=std::string::npos;
  const bool control=player && client->config.method!=HTTP_METHOD_GET;
  const bool artwork=split_spotify && client->config.url && std::string(client->config.url).find("https://i.scdn.co/image/")==0;
  requests.push_back({client->config.url,client->request_headers,client->config.method,
                      client->config.crt_bundle_attach!=nullptr,client->config.disable_auto_redirect});
  if(control)++control_calls;else if(artwork)++artwork_calls;
  else if(player)++player_calls;else ++token_calls;
  if (before_perform) before_perform();
  if(control && before_control)before_control();
  if (slow) esphome::transport_now += 8000;
  const std::string &selected_response=control?control_response:artwork?artwork_response:((split_spotify && player)?player_response:response);
  client->selected_status=control?control_status:artwork?artwork_status:((split_spotify && player)?player_status:status);
  auto &selected_headers=control?control_headers:artwork?artwork_headers:response_headers;
  for(auto &[key,value]:selected_headers) {
    esp_http_client_event_t event{HTTP_EVENT_ON_HEADER,client->config.user_data,key.data(),value.data()};
    if(client->config.event_handler(&event)!=ESP_OK)return ESP_FAIL;
  }
  for(size_t at=0;at<selected_response.size();at+=7) {
    esp_http_client_event_t event{HTTP_EVENT_ON_DATA,client->config.user_data};
    event.data=null_data?nullptr:const_cast<char *>(selected_response.data()+at);
    event.data_len=negative_data?-1:int(std::min<size_t>(7,selected_response.size()-at));
    if(client->config.event_handler(&event)!=ESP_OK)return ESP_FAIL;
  }
  return fail_perform?ESP_FAIL:ESP_OK;
}
inline int esp_http_client_get_status_code(HostClient *client) {return client->selected_status;}
inline bool esp_http_client_is_complete_data_received(HostClient *) {return http_fixture::complete;}
inline int esp_http_client_cleanup(HostClient *client) {++http_fixture::cleanups;delete client;return ESP_OK;}
