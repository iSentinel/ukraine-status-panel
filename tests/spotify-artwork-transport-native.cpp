#include "artwork_transport.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esphome/components/network/util.h"
#include "esphome/components/usp_weather/network_gate.h"
#include <array>
#include <cassert>
#include <cstdio>
using namespace esphome::usp_spotify_auth;
namespace f = http_fixture;
std::array<uint8_t, usp_spotify_artwork_http::MAX_BODY> output{};
usp_spotify_artwork::Request request() {
  usp_spotify_artwork::RequestPlan p;
  p.select("https://i.scdn.co/image/albumA");
  return p.begin(0);
}
void reset() {
  esphome::usp_network::host_expected_owner="spotify-artwork";
  f::reset();
  f::response_headers={{"Content-Type","image/jpeg"}};
  f::response="synthetic-jpeg-bytes";
}
void configuration() {
  reset(); IdfArtworkTransport transport;
  const auto owned=request();
  auto result=transport.perform(owned,output.data(),output.size());
  assert(result.complete && result.attempted && result.size==f::response.size());
  assert(std::string(f::config.url)=="https://i.scdn.co/image/albumA");
  assert(f::config.transport_type==HTTP_TRANSPORT_OVER_SSL);
  assert(f::config.method==HTTP_METHOD_GET && f::posted.empty());
  assert(f::config.disable_auto_redirect && f::config.max_authorization_retries==-1);
  assert(!f::config.skip_cert_common_name_check && !f::config.keep_alive_enable);
  assert(f::config.crt_bundle_attach==esp_crt_bundle_attach);
  assert(f::headers.size()==2 && !f::headers.count("Authorization"));
  assert(f::headers.at("Accept")=="image/jpeg");
  assert(f::headers.at("Accept-Encoding")=="identity");
  assert(f::cleanups==1 && esphome::usp_network::host_active==0);
}
void validation() {
  reset(); IdfArtworkTransport transport;
  auto bad=request(); bad.url.fill('x');
  assert(!transport.perform(bad,output.data(),output.size()).attempted);
  assert(!transport.perform(request(),nullptr,output.size()).attempted);
  assert(!transport.perform(request(),output.data(),output.size()+1).attempted);
  esphome::network::host_connected=false;
  assert(!transport.perform(request(),output.data(),output.size()).attempted);
  esphome::network::host_connected=true;
  esphome::usp_network::host_admitted=false;
  assert(!transport.perform(request(),output.data(),output.size()).attempted);
  esphome::usp_network::host_admitted=true;
  assert(f::calls==0);
}
void failures() {
  for (unsigned mode=0;mode<9;++mode) {
    reset(); IdfArtworkTransport transport;
    if(mode==0) f::fail_init=true;
    if(mode==1) f::fail_prepare=true;
    if(mode==2) f::fail_perform=true;
    if(mode==3) f::complete=false;
    if(mode==4) f::null_data=true;
    if(mode==5) f::negative_data=true;
    if(mode==6) f::slow=true;
    if(mode==7) f::response=std::string(output.size()+1,'a');
    if(mode==8) f::status=302;
    auto result=transport.perform(request(),output.data(),output.size());
    assert(!result.complete && result.size<=output.size());
    assert(esphome::usp_network::host_active==0);
    if(mode!=0) assert(f::cleanups==1);
  }
}
void cooldown() {
  reset(); IdfArtworkTransport transport;
  f::status=429; f::response="{}";
  f::response_headers={{"Content-Type","application/json"},{"Retry-After","3600"}};
  auto result=transport.perform(request(),output.data(),output.size());
  assert(result.status==429 && !result.complete && result.retry.delay==3600000);
  esphome::transport_now=3600099;
  assert(!transport.perform(request(),output.data(),output.size()).attempted);
  reset(); esphome::transport_now=3600100;
  assert(transport.perform(request(),output.data(),output.size()).complete);
  reset(); IdfArtworkTransport blocked;
  f::status=429; f::response_headers={{"Retry-After","86401"}};
  assert(blocked.perform(request(),output.data(),output.size()).retry.blocked);
  reset(); esphome::transport_now=86400100;
  assert(!blocked.perform(request(),output.data(),output.size()).attempted);
}
bool current(void *owner) { return *static_cast<bool *>(owner); }
void cancellation() {
  reset(); IdfArtworkTransport transport; bool live=false;
  assert(!transport.perform(request(),output.data(),output.size(),current,&live).attempted);
  assert(f::calls==0); live=true;
  f::before_perform=[&]{ live=false; };
  auto result=transport.perform(request(),output.data(),output.size(),current,&live);
  assert(result.cancelled && !result.complete && f::cleanups==1);
}
int main() {
#ifdef HOST_EXPECT_NO_BUNDLE
  reset(); IdfArtworkTransport transport;
  assert(!IdfArtworkTransport::available());
  assert(!transport.perform(request(),output.data(),output.size()).attempted);
  assert(f::calls==0);
  puts("ARTWORK_IDF_NO_BUNDLE=PASS"); return 0;
#endif
  configuration(); validation(); failures(); cooldown(); cancellation();
  puts("ARTWORK_IDF_TRANSPORT_NATIVE=PASS fake_IDF only no_provider no_device");
}
