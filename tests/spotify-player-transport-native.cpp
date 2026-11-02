#include "playback_transport.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esphome/components/network/util.h"
#include "esphome/components/usp_weather/network_gate.h"
#include <array>
#include <cassert>
#include <iostream>
#include <string>
using esphome::usp_spotify_auth::IdfPlaybackTransport;
using namespace usp_spotify_tokens;
namespace fixture=http_fixture;

HttpResult perform(std::string token="access_A", size_t capacity=32768) {
  static std::array<char,32769> output{};
  output.fill(0);
  IdfPlaybackTransport transport;
  const auto result=transport.perform(token,output.data(),capacity);
  assert(esphome::usp_network::host_active==0);
  return result;
}
void configuration() {
  fixture::reset();fixture::response=R"({"is_playing":false,"item":null,"progress_ms":null,"device":null})";
  const auto result=perform();
  assert(result.attempted && result.complete && result.size==fixture::response.size());
  const auto &config=fixture::config;
  assert(std::string(config.url)=="https://api.spotify.com/v1/me/player?additional_types=episode");
  assert(config.method==HTTP_METHOD_GET && config.transport_type==HTTP_TRANSPORT_OVER_SSL);
  assert(config.disable_auto_redirect && config.max_authorization_retries==-1);
  assert(!config.skip_cert_common_name_check && config.crt_bundle_attach==esp_crt_bundle_attach);
  assert(config.timeout_ms==5000 && !config.keep_alive_enable);
  assert(fixture::headers.at("Authorization")=="Bearer access_A");
  assert(fixture::headers.at("Accept")=="application/json");
  assert(fixture::headers.at("Accept-Encoding")=="identity");
  assert(fixture::headers.count("Content-Type")==0 && fixture::posted.empty());
}
void admission() {
  fixture::reset();esphome::network::host_connected=false;
  assert(!perform().attempted && fixture::calls==0);
  esphome::network::host_connected=true;esphome::usp_network::host_admitted=false;
  assert(!perform().attempted && fixture::calls==0);
  esphome::usp_network::host_admitted=true;
  assert(!perform("").attempted && fixture::calls==0);
  assert(!perform("bad token").attempted && fixture::calls==0);
  fixture::fail_init=true;assert(!perform().attempted && fixture::calls==0);
  fixture::fail_init=false;fixture::fail_prepare=true;
  assert(!perform().attempted && fixture::calls==0 && fixture::cleanups==1);
}
void failures() {
  fixture::reset();fixture::status=302;auto result=perform();
  assert(result.status==302 && fixture::calls==1 && fixture::config.disable_auto_redirect);
  for(int mode=0;mode<6;++mode) {
    fixture::reset();fixture::response="body";
    if(mode==0)fixture::fail_perform=true;
    if(mode==1)fixture::complete=false;
    if(mode==2)fixture::null_data=true;
    if(mode==3)fixture::negative_data=true;
    if(mode==4)fixture::response=std::string(32769,'x');
    if(mode==5)fixture::slow=true;
    result=perform();
    assert(result.attempted && !result.complete && fixture::calls==1 && fixture::cleanups==1);
    assert(result.size<=32768);
  }
}
void statuses_and_retry() {
  for(int status:{200,204,401,403,429,500}) {
    fixture::reset();fixture::status=status;fixture::response=status==204?"":"{}";
    const auto result=perform();assert(result.attempted && result.complete && result.status==status);
  }
  fixture::reset();fixture::status=429;
  fixture::response_headers={{"retry-after","60"},{"ReTrY-AfTeR"," 120 "}};
  auto result=perform();assert(result.retry_valid && result.retry_ms==120000);
  fixture::response_headers={{"Retry-After","9999999999999999999999999"}};
  result=perform();assert(result.retry_excess && !result.retry_valid);
}
int main() {
#ifdef HOST_EXPECT_NO_BUNDLE
  assert(!IdfPlaybackTransport::available());fixture::reset();assert(!perform().attempted);
  std::cout<<"SPOTIFY_PLAYER_TRANSPORT_NO_BUNDLE=PASS no_unverified_TLS\n";return 0;
#endif
  assert(IdfPlaybackTransport::available());configuration();admission();failures();statuses_and_retry();
  std::cout<<"SPOTIFY_PLAYER_TRANSPORT_NATIVE=PASS fixed_GET bearer TLS no_redirect admission bounded retry_after statuses no_network\n";
}
