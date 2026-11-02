#include "token_transport.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esphome/components/network/util.h"
#include "esphome/components/usp_weather/network_gate.h"
#include <iostream>
using esphome::usp_spotify_auth::IdfTokenTransport;
using namespace usp_spotify_tokens;
namespace fixture=http_fixture;

HttpResult perform(size_t capacity=MAX_BODY) {
  static std::array<char,MAX_BODY+1> output{};
  output.fill(0);
  IdfTokenTransport transport;
  const auto result=transport.perform("fixture=code",12,output.data(),capacity);
  assert(esphome::usp_network::host_active==0);
  return result;
}

/** Fixed TLS configuration cannot redirect a code or fall back to an unchecked certificate. */
void configuration() {
  fixture::reset();fixture::response=R"({"access_token":"fixture"})";
  const auto result=perform();
  assert(result.attempted && result.complete && result.size==fixture::response.size());
  assert(fixture::calls==1 && fixture::cleanups==1);
  const auto &config=fixture::config;
  assert(std::string(config.url)=="https://accounts.spotify.com/api/token");
  assert(config.method==HTTP_METHOD_POST && config.transport_type==HTTP_TRANSPORT_OVER_SSL);
  assert(config.disable_auto_redirect && config.max_authorization_retries==-1);
  assert(!config.skip_cert_common_name_check && config.crt_bundle_attach==esp_crt_bundle_attach);
  assert(config.timeout_ms==5000 && !config.keep_alive_enable);
  assert(fixture::headers.at("Content-Type")=="application/x-www-form-urlencoded");
  assert(fixture::headers.count("Authorization")==0);
  assert(fixture::posted=="fixture=code");
}

/** Network/admission/allocation/setup failures never call perform. */
void admission() {
  fixture::reset();esphome::network::host_connected=false;
  assert(!perform().attempted && fixture::calls==0);
  esphome::network::host_connected=true;esphome::usp_network::host_admitted=false;
  assert(!perform().attempted && fixture::calls==0);
  esphome::usp_network::host_admitted=true;
  fixture::fail_init=true;assert(!perform().attempted && fixture::calls==0);
  fixture::fail_init=false;fixture::fail_prepare=true;
  assert(!perform().attempted && fixture::calls==0 && fixture::cleanups==1);
}

/** Redirects, partial transfer, overflow and timeout never yield a confirmed token reply. */
void failures() {
  fixture::reset();fixture::status=302;auto result=perform();
  assert(result.status==302 && fixture::calls==1 && fixture::config.disable_auto_redirect);
  for(int mode=0;mode<6;++mode) {
    fixture::reset();fixture::response="body";
    if(mode==0)fixture::fail_perform=true;
    if(mode==1)fixture::complete=false;
    if(mode==2)fixture::null_data=true;
    if(mode==3)fixture::negative_data=true;
    if(mode==4)fixture::response=std::string(MAX_BODY+1,'x');
    if(mode==5)fixture::slow=true;
    result=perform();
    assert(result.attempted && !result.complete && fixture::calls==1 && fixture::cleanups==1);
    assert(result.size<=MAX_BODY);
  }
}

/** Duplicate numeric Retry-After uses the maximum; excessive delay suspends automatic retry. */
void retry_after() {
  fixture::reset();fixture::status=429;
  fixture::response_headers={{"retry-after","60"},{"ReTrY-AfTeR"," 120 "}};
  auto result=perform();assert(result.retry_valid && result.retry_ms==120000);
  fixture::response_headers={{"Retry-After","9999999999999999999999999"}};
  result=perform();assert(result.retry_excess && !result.retry_valid);
  for(const char *value:{"-1","+2","1e3","Sun, 06 Nov 1994 08:49:37 GMT","","  "}) {
    fixture::response_headers={{"Retry-After",value}};
    result=perform();assert(!result.retry_valid && !result.retry_excess);
  }
}
int main() {
#ifdef HOST_EXPECT_NO_BUNDLE
  assert(!IdfTokenTransport::available());
  fixture::reset();assert(!perform().attempted && fixture::calls==0);
  std::cout<<"SPOTIFY_TOKEN_TRANSPORT_NO_BUNDLE=PASS no_unverified_TLS\n";
  return 0;
#endif
  assert(IdfTokenTransport::available());
  configuration();admission();failures();retry_after();
  std::cout<<"SPOTIFY_TOKEN_TRANSPORT_PREFLIGHT=PASS actual_transport SDK_fixture fixed_TLS cert redirect_auth_retry_disabled admission timeout overflow incomplete retry_after no_network\n";
}
