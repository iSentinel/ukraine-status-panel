#include "spotify_auth.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include <cassert>
#include <iostream>
#include <thread>
/** Host-only controls emulate a worker becoming unavailable without real I/O. */
class SpotifyAuthApi : public esphome::usp_spotify_auth::SpotifyAuthApi {
 public:
  void set_exchange_available(bool value) {this->exchange_available_.store(value);}
  bool take_token_request(usp_spotify_auth_return::TokenRequest *out) {
    uint32_t generation=0,accepted=0;return this->take(out,&generation,&accepted);
  }
};
using Request=esphome::web_server_idf::AsyncWebServerRequest;
using esphome::host_time;
constexpr const char *BASE="/usp/spotify/auth/";
constexpr const char *CLIENT="client123";
constexpr const char *REDIRECT="https://return.test/cb";
void configure_application(SpotifyAuthApi &api) {
  api.set_application_client_id(CLIENT);api.set_application_redirect_uri(REDIRECT);api.set_session_key_id(5);
}
std::string path(const char *suffix) {return std::string(BASE)+suffix;}
/** Exercise the actual body and request callbacks with arbitrary chunk sizes. */
std::string post(SpotifyAuthApi &api,const char *route,std::string body,int expected,
                 size_t chunk=7,const char *type="text/plain") {
  Request request(path(route),HTTP_POST,body.size(),type);
  assert(api.canHandle(&request));
  for(size_t at=0;at<body.size();at+=chunk)
    api.handleBody(&request,reinterpret_cast<uint8_t *>(body.data()+at),
                   std::min(chunk,body.size()-at),at,body.size());
  api.handleRequest(&request);assert(request.status==expected);
  assert(request.response_headers().at("Cache-Control")=="no-store");
  assert(request.response_headers().at("Referrer-Policy")=="no-referrer");
  return request.body;
}
std::string start(SpotifyAuthApi &api) {
  const auto body=post(api,"begin","client_id=client123&redirect_uri=https%3A%2F%2Freturn.test%2Fcb",200);
  const auto pos=body.find("&state=");assert(pos!=std::string::npos);
  return body.substr(pos+7,22);
}
void lifecycle(SpotifyAuthApi &api) {
  host_time=100;auto state=start(api);
  post(api,"begin","client_id=client123&redirect_uri=https%3A%2F%2Freturn.test%2Fcb",409);
  post(api,"callback","state=wrong&code=wrong",400);
  const auto result=post(api,"callback","state="+state+"&code=opaque%2B%2F%25",202,1);
  assert(result.find("opaque")==std::string::npos && result.find("verifier")==std::string::npos);
  post(api,"callback","state="+state+"&code=replay",400);
  usp_spotify_auth_return::TokenRequest output;
  assert(api.take_token_request(&output));
  assert(std::string(output.code.data())=="opaque+/%");
  assert(!api.take_token_request(&output) && output.code[0]==0);
  post(api,"cancel","",200);state=start(api);post(api,"callback","state="+state+"&error=access_denied",200);
  post(api,"cancel","",200);
  state=start(api);host_time=300100;api.loop();
  post(api,"callback","state="+state+"&code=late",400);
  state=start(api);post(api,"callback","state="+state+"&code=pending",202);
  host_time=360100;api.loop();assert(!api.take_token_request(&output));
}
void malformed(SpotifyAuthApi &api) {
  post(api,"begin","client_id=other123&redirect_uri=https%3A%2F%2Freturn.test%2Fcb",400);
  post(api,"begin","client_id=client123&redirect_uri=https%3A%2F%2Fother.test%2Fcb",400);
  post(api,"begin","client_id=x&redirect_uri=http%3A%2F%2F192.168.1.2%2F",400);
  post(api,"callback","state=x&state=y&code=z",400);
  post(api,"callback","state=x&code=%00",400);
  post(api,"callback","state=x&code=z",400,7,"application/json");
  Request query(path("begin")+"?code=not-allowed",HTTP_POST,0);
  api.handleRequest(&query);assert(query.status==400);
  Request method(path("begin"),HTTP_GET);
  api.handleRequest(&method);assert(method.status==405);
  Request missing(path("begin"),HTTP_POST,30);
  api.handleRequest(&missing);assert(missing.status==400);
  std::string data(4097,'x');post(api,"callback",data,400,128);
  Request order(path("callback"),HTTP_POST,10);
  uint8_t bytes[3]{'a','b','c'};api.handleBody(&order,bytes,3,1,10);
  api.handleRequest(&order);assert(order.status==400);
  Request overflow(path("callback"),HTTP_POST,10);
  api.handleBody(&overflow,bytes,3,0,10);
  api.handleBody(&overflow,bytes,3,SIZE_MAX,10);
  api.handleRequest(&overflow);assert(overflow.status==400);
  post(api,"cancel","",200);
}
void rng_failure(SpotifyAuthApi &api) {
  post(api,"cancel","",200);
  esphome::host_rng_fail=true;
  post(api,"begin","client_id=client123&redirect_uri=https%3A%2F%2Freturn.test%2Fcb",503);
  esphome::host_rng_fail=false;
  Request status(path("status"),HTTP_GET);api.handleRequest(&status);
  assert(status.status==200 && status.body.find("idle")!=std::string::npos);
}
int concurrent_request(SpotifyAuthApi &api,const char *route,std::string body,
                       std::string *reply) {
  Request request(path(route),HTTP_POST,body.size());
  if(!body.empty())api.handleBody(&request,reinterpret_cast<uint8_t *>(body.data()),
                                  body.size(),0,body.size());
  api.handleRequest(&request);
  *reply=request.body;
  return request.status;
}
void concurrency(SpotifyAuthApi &api) {
  host_time=400000;
  std::atomic<bool> done{false};
  std::thread main_loop([&]() {while(!done.load())api.loop();});
  for(int i=0;i<1000;++i) {
    std::string reply;
    const int begun=concurrent_request(api,"begin",
        "client_id=client123&redirect_uri=https%3A%2F%2Freturn.test%2Fcb",&reply);
    assert(begun==200 || begun==409 || begun==503);
    if(begun==200) {
      const auto at=reply.find("&state=");assert(at!=std::string::npos);
      const std::string state=reply.substr(at+7,22);
      const int callback=concurrent_request(api,"callback","state="+state+"&code=threaded",&reply);
      assert(callback==202 || callback==503);
      usp_spotify_auth_return::TokenRequest output;
      if(api.take_token_request(&output))assert(std::string(output.code.data())=="threaded");
    }
    const int cancelled=concurrent_request(api,"cancel","",&reply);
    assert(cancelled==200 || cancelled==503);
  }
  done=true;main_loop.join();
  post(api,"cancel","",200);
  usp_spotify_auth_return::TokenRequest output;assert(!api.take_token_request(&output));
}

/** Default production state must refuse sign-in until the worker is ready. */
void availability(SpotifyAuthApi &api) {
  api.set_exchange_available(false);
  Request initial(path("status"),HTTP_GET);api.handleRequest(&initial);
  assert(initial.body.find("\"exchange_available\":false")!=std::string::npos);
  assert(initial.body.find("\"client_id\":\"client123\"")!=std::string::npos);
  assert(initial.body.find("\"redirect_uri\":\"https://return.test/cb\"")!=std::string::npos);
  assert(initial.body.find("access_token")==std::string::npos && initial.body.find("refresh_token")==std::string::npos);
  const auto rng_before=esphome::host_rng_fail;
  post(api,"begin","client_id=client123&redirect_uri=https%3A%2F%2Freturn.test%2Fcb",503);
  post(api,"callback","state=wrong&code=wrong",503);
  assert(esphome::host_rng_fail==rng_before);
  usp_spotify_auth_return::TokenRequest output;
  assert(!api.take_token_request(&output));
  api.set_exchange_available(true);
  const auto state=start(api);
  post(api,"callback","state="+state+"&code=discard-me",202);
  api.set_exchange_available(false);
  assert(!api.take_token_request(&output));
  api.loop();
  api.set_exchange_available(true);
  assert(!api.take_token_request(&output));
  uint8_t language=2;
  api.set_language([&language](){return language;});api.loop();
  Request english(path("status"),HTTP_GET);api.handleRequest(&english);
  assert(english.body.find("\"language\":\"en\"")!=std::string::npos);
  language=250;api.loop();
  Request fallback(path("status"),HTTP_GET);api.handleRequest(&fallback);
  assert(fallback.body.find("\"language\":\"uk\"")!=std::string::npos);
  api.set_language({});
}

void private_assets(SpotifyAuthApi &api) {
  for(const char *name:{"","page.js","page.css","return.js"}) {
    Request request(path(name),HTTP_GET);assert(api.canHandle(&request));
    api.handleRequest(&request);
    assert(request.status==200 && request.body.size()>100);
    assert(static_cast<uint8_t>(request.body[0])==0x1f && static_cast<uint8_t>(request.body[1])==0x8b);
    assert(request.response_headers().at("Content-Encoding")=="gzip");
    assert(request.response_headers().at("Cache-Control")=="no-store");
    assert(request.response_headers().at("Content-Security-Policy").find("frame-ancestors 'none'")!=std::string::npos);
    Request forbidden(path(name),HTTP_POST);api.handleRequest(&forbidden);assert(forbidden.status==405);
    Request query(path(name)+"?code=secret",HTTP_GET);api.handleRequest(&query);assert(query.status==400);
  }
}
void actual_adapter_cases() {
  esphome::web_server_base::WebServerBase base;
  esphome::web_server_base::global_web_server_base=&base;
  SpotifyAuthApi unconfigured;unconfigured.setup();assert(unconfigured.is_failed());
  SpotifyAuthApi api;configure_application(api);api.setup();
#ifdef USE_WEBSERVER_AUTH
  assert(!api.is_failed() && base.protected_handlers==1);
  private_assets(api);availability(api);lifecycle(api);malformed(api);rng_failure(api);concurrency(api);
  esphome::host_alloc_fail=true;
  SpotifyAuthApi failed;configure_application(failed);failed.setup();assert(failed.is_failed() && base.protected_handlers==1);
  esphome::host_alloc_fail=false;
  esphome::web_server_base::global_web_server_base=nullptr;
  SpotifyAuthApi missing;configure_application(missing);missing.setup();assert(missing.is_failed());
#else
  assert(api.is_failed() && base.protected_handlers==0);
  std::cout<<"SPOTIFY_AUTH_ADAPTER_NOAUTH=PASS registration_blocked\n";
  return;
#endif
  std::cout<<"SPOTIFY_AUTH_ADAPTER_NATIVE=PASS actual_entrypoints strict_body chunking callback_once privacy expiry main_try_lock concurrency1000 auth_gate allocation_fail no_provider\n";
}

int main(){actual_adapter_cases();host_tasks::join_all();}
