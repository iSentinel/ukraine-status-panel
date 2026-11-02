#include "web_server_base.h"
#include "components/web_server_idf/digest_auth_state.h"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
extern "C" unsigned char *MD5(const unsigned char *, size_t, unsigned char *);
struct md5_context_t { std::string bytes; };
void esp_rom_md5_init(md5_context_t *ctx) { ctx->bytes.clear(); }
void esp_rom_md5_update(md5_context_t *ctx, const void *p, size_t n) {
  ctx->bytes.append(static_cast<const char *>(p), n);
}
void esp_rom_md5_final(unsigned char *out, md5_context_t *ctx) {
  MD5(reinterpret_cast<const unsigned char *>(ctx->bytes.data()), ctx->bytes.size(), out);
}
void format_hex_to(char *out, size_t capacity, const uint8_t *bytes, size_t size) {
  assert(capacity >= size * 2 + 1);
  const char *hex = "0123456789abcdef";
  for (size_t i = 0; i < size; ++i) {out[2*i]=hex[bytes[i]>>4];out[2*i+1]=hex[bytes[i]&15];}
  out[2*size]=0;
}
struct StringRef {
  std::string_view value;
  StringRef() = default;
  StringRef(const char *p, size_t n) : value(p,n) {}
  const char *c_str() const { return value.data(); }
  size_t size() const { return value.size(); }
  char operator[](size_t i) const { return value[i]; }
  bool operator!=(const char *other) const { return value != other; }
};
using Mutex = std::mutex;
using LockGuard = std::lock_guard<std::mutex>;
uint32_t fixture_now = 1000;
uint32_t millis() { return fixture_now; }
namespace esphome::web_server_idf {
#include "digest-production.inc"
}
namespace digest = esphome::web_server_idf;
using esphome::web_server_base::internal::AuthMiddlewareHandler;
using esphome::web_server_base::internal::Credentials;
#include "signed-fixtures.inc"

/** Call actual production parsing, MD5 verification and replay state together. */
bool verify(const char *password, const char *method, const char *uri,
            const std::string &header, std::array<char,33> &request_state) {
#if USP_DIGEST_HAS_REQUEST_STATE
  return digest::check_digest_auth("admin",password,header,method,uri,request_state);
#else
  (void)request_state;
  return digest::check_digest_auth("admin",password,header,method,uri);
#endif
}
struct Target : AsyncWebHandler {
  unsigned body{0}, upload{0}, requests{0};
  void handleBody(AsyncWebServerRequest *,uint8_t *,size_t,size_t,size_t) override {++body;}
  void handleUpload(AsyncWebServerRequest *,const std::string &,size_t,uint8_t *,size_t,bool) override {++upload;}
  void handleRequest(AsyncWebServerRequest *) override {++requests;}
};

/** Reproduce real callbacks: every body fragment, then request dispatch. */
int repeated_callbacks(bool baseline) {
  digest::remember_digest_nonce(NONCE,OPAQUE,fixture_now);
  Credentials credentials;credentials.set_username("admin");credentials.set_password(PASSWORD);
  Target target;AuthMiddlewareHandler middleware(&target,&credentials);
  AsyncWebServerRequest request;std::array<char,33> state{};
  request.url_value=URI;request.origin="http://panel.local";request.method_value=HTTP_POST;
  request.verifier=[&](const char *,const char *password) {
    return verify(password,"POST",URI,HEADER,state);
  };
  uint8_t byte{};
  middleware.handleBody(&request,&byte,1,0,2);
  middleware.handleBody(&request,&byte,1,1,2);
  middleware.handleUpload(&request,"fixture",0,&byte,1,true);
  middleware.handleRequest(&request);
  if (baseline) {
    assert(target.body==1 && target.requests==0 && request.challenges==3);
    std::cout<<"DIGEST_DUPLICATE_CALLBACK_BEFORE=REPRODUCED first_body1 request0 challenges3\n";
    return 0;
  }
  assert(target.body==2 && target.upload==1 && target.requests==1);
  assert(request.auth_calls==4 && request.challenges==0);
  assert(!verify("wrong-password","POST",URI,HEADER,state));
  assert(!verify(PASSWORD,"GET",URI,HEADER,state));
  assert(!verify(PASSWORD,"POST","/usp/spotify/auth/cancel",HEADER,state));
  std::array<char,33> replay{};
  assert(!verify(PASSWORD,"POST",URI,HEADER,replay));
  assert(verify(PASSWORD,"POST",URI,HEADER2,state));
  std::array<char,33> replay2{};
  assert(!verify(PASSWORD,"POST",URI,HEADER2,replay2));
  return 0;
}

/** First invalid admission must not spend the valid request's nonce count. */
void negative_boundaries() {
  digest::remember_digest_nonce(NONCE3,OPAQUE,fixture_now);
  std::array<char,33> state{};
  assert(!verify("wrong","POST",URI,HEADER3,state));
  assert(verify(PASSWORD,"POST",URI,HEADER3,state));
  auto altered=std::string(HEADER3);altered.back()='x';
  assert(!verify(PASSWORD,"POST",URI,altered,state));
  digest::remember_digest_nonce(NONCE4,OPAQUE,fixture_now);
  fixture_now+=digest::digest_auth::NONCE_TTL_MS;
  std::array<char,33> expired{};
  assert(!verify(PASSWORD,"POST",URI,HEADER4,expired));
}
/** Reset only synthetic host state between independent protocol cases. */
static void reset_stale_fixture() {
  digest::digest_nonce_store = digest::digest_auth::NonceStore{};
  fixture_now = 1000;
}

/** Expired and unknown nonces must fail authorization with a retry hint. */
static void stale_expiry_and_restart() {
  reset_stale_fixture();
  digest::remember_digest_nonce(NONCE, OPAQUE, fixture_now);
  fixture_now += digest::digest_auth::NONCE_TTL_MS;
  std::array<char, 33> state{};
  bool stale = false;
  assert(!digest::check_digest_auth("admin", PASSWORD, HEADER, "POST", URI, state, &stale));
  assert(stale && state[0] == 0);
  reset_stale_fixture();
  stale = false;
  assert(!digest::check_digest_auth("admin", PASSWORD, HEADER, "POST", URI, state, &stale));
  assert(stale && state[0] == 0);
}

/** Bad credentials, method and URI must not be reported as a stale nonce. */
static void stale_negative_credentials() {
  reset_stale_fixture();
  digest::remember_digest_nonce(NONCE, OPAQUE, fixture_now);
  std::array<char, 33> state{};
  bool stale = true;
  assert(!digest::check_digest_auth("admin", "wrong", HEADER, "POST", URI, state, &stale));
  assert(!stale);
  stale = true;
  assert(!digest::check_digest_auth("admin", PASSWORD, HEADER, "GET", URI, state, &stale));
  assert(!stale);
  stale = true;
  assert(!digest::check_digest_auth("admin", PASSWORD, HEADER, "POST", "/wrong", state, &stale));
  assert(!stale);
  assert(digest::check_digest_auth("admin", PASSWORD, HEADER, "POST", URI, state, &stale));
  assert(!stale);
}

/** Wrong opaque and replay stay rejected; repeated callbacks retain admission. */
static void stale_negative_opaque_and_replay() {
  reset_stale_fixture();
  digest::remember_digest_nonce(NONCE, OPAQUE, fixture_now);
  auto altered = std::string(HEADER);
  const auto start = altered.find("opaque=\"");
  assert(start != std::string::npos);
  altered.replace(start + 8, 32, std::string(32, 'f'));
  std::array<char, 33> state{};
  bool stale = true;
  assert(!digest::check_digest_auth("admin", PASSWORD, altered, "POST", URI, state, &stale));
  assert(!stale);
  assert(digest::check_digest_auth("admin", PASSWORD, HEADER, "POST", URI, state, &stale));
  assert(!stale);
  assert(digest::check_digest_auth("admin", PASSWORD, HEADER, "POST", URI, state, &stale));
  assert(!stale);
  std::array<char, 33> replay{};
  assert(!digest::check_digest_auth("admin", PASSWORD, HEADER, "POST", URI, replay, &stale));
  assert(!stale);
}

static void stale_protocol_cases() {
  stale_expiry_and_restart();
  stale_negative_credentials();
  stale_negative_opaque_and_replay();
  reset_stale_fixture();
  std::cout << "DIGEST_STALE_NATIVE=PASS expired unknown wrong_password method uri opaque replay cached_request\n";
}

int main(int argc,char **argv) {
  stale_protocol_cases();
  const bool baseline=argc==2 && std::string(argv[1])=="--expect-duplicate";
  repeated_callbacks(baseline);
  if (baseline) return 0;
  negative_boundaries();
  std::cout<<"DIGEST_REQUEST_NATIVE=PASS actual_digest_and_middleware fragments upload request replay wrong_credentials method uri expiry\n";
}
