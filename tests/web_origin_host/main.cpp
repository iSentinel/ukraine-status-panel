#include "web_server_base.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using esphome::web_server_base::internal::AuthMiddlewareHandler;
using esphome::web_server_base::internal::Credentials;
namespace policy = esphome::web_server_base::origin_policy;

struct Target : AsyncWebHandler {
  unsigned requests = 0, bodies = 0, uploads = 0;
  void handleRequest(AsyncWebServerRequest *) override { ++requests; }
  void handleBody(AsyncWebServerRequest *, uint8_t *, size_t, size_t, size_t) override { ++bodies; }
  void handleUpload(AsyncWebServerRequest *, const std::string &, size_t, uint8_t *, size_t, bool) override { ++uploads; }
  unsigned calls() const { return requests + bodies + uploads; }
};

/** Exercise each actual authentication callback before its target callback. */
struct Harness {
  Credentials credentials;
  Target target;
  AuthMiddlewareHandler middleware{&target, &credentials};
  Harness() {
    credentials.set_username("admin");
    credentials.set_password("synthetic-fixture");
  }
  void invoke(AsyncWebServerRequest &request) {
    uint8_t data[1]{};
    middleware.handleBody(&request, data, 1, 0, 1);
    middleware.handleUpload(&request, "fixture", 0, data, 1, true);
    middleware.handleRequest(&request);
  }
};

/** Supply decoded URL and header metadata at the real middleware boundary. */
AsyncWebServerRequest request_for(const std::string &origin,
                                 const std::string &host = "panel.local",
                                 const std::string &url = "/usp/ota/preflight/status") {
  AsyncWebServerRequest request;
  request.origin = origin;
  request.host = host;
  request.url_value = url;
  request.method_value = HTTP_POST;
  request.verifier = [](const char *user, const char *password) {
    assert(std::string(user) == "admin");
    assert(std::string(password) == "synthetic-fixture");
    return true;
  };
  return request;
}

/** Reject other sites on every callback and method of custom routes. */
void check_blocked() {
  for (auto method : {HTTP_GET, HTTP_POST, HTTP_HEAD, HTTP_OPTIONS,
                      HTTP_PUT, HTTP_PATCH, HTTP_DELETE}) {
    Harness harness;
    auto request = request_for("http://other.invalid");
    request.method_value = method;
    harness.invoke(request);
    assert(harness.target.calls() == 0);
    assert(request.response_status == 403 && request.challenges == 0);
    assert(request.auth_calls == 1);
  }
  for (const char *origin : {"null", "https://panel.local", "http://panel.local/path",
                             "http://panel.local.evil", "http://evil@panel.local"}) {
    Harness harness;
    auto request = request_for(origin);
    harness.invoke(request);
    assert(harness.target.calls() == 0 && request.response_status == 403);
  }
  Harness missing_host;
  auto request = request_for("http://panel.local", "");
  missing_host.invoke(request);
  assert(missing_host.target.calls() == 0 && request.response_status == 403);
}

/** Preserve own-page requests and clients that send no Origin header. */
void check_compatible() {
  const std::vector<std::pair<std::string, std::string>> allowed = {
    {"", ""}, {"", "panel.local"}, {"http://panel.local", "panel.local"},
    {"HTTP://PANEL.LOCAL:80", "panel.local"},
    {"http://192.0.2.208", "192.0.2.208"},
    {"http://panel.local:8080", "panel.local:8080"},
    {"http://[fe80::abcd]:80", "[FE80::ABCD]"}
  };
  for (const auto &[origin, host] : allowed) {
    Harness harness;
    auto request = request_for(origin, host);
    harness.invoke(request);
    assert(harness.target.calls() == 3);
    assert(request.response_status == 0 && request.challenges == 0);
  }
}

/** Leave non-custom routes to their existing WebServer Origin policy. */
void check_scope_and_authentication() {
  for (const char *url : {"/", "/events", "/0.js", "/button/Restart/press",
                          "/switch/Test/toggle", "/uspx/ota"}) {
    Harness harness;
    auto request = request_for("http://other.invalid", "panel.local", url);
    harness.invoke(request);
    assert(harness.target.calls() == 3);
  }
  Harness denied;
  auto request = request_for("http://other.invalid");
  request.verifier = [](const char *, const char *) { return false; };
  denied.invoke(request);
  assert(denied.target.calls() == 0 && request.response_status == 401);
  assert(request.challenges == 1 && request.auth_calls == 1);
  assert(request.header_reads == 0);
}

/** Validate host boundaries and default port handling with no HTTP doubles. */
void check_parser() {
  for (const char *origin : {"", "null", "https://panel.local", "file://panel.local",
       "http://panel.local:0", "http://panel.local:65536", "http://panel.local:81",
       "http://panel.local:80:90", "http://panel.local/", "http://panel.local?x",
       "http://panel.local#x", "http://panel.local@evil", "http://evil@panel.local",
       "http://panel.local\\evil", "http://panel.local http://other",
       "http://panel.local, http://other", " http://panel.local"}) {
    assert(!policy::matches(origin, "panel.local"));
  }
  policy::Authority authority;
  for (const char *host : {"", "a/b", "a@b", "a?b", "a#b", "a b", "a:0",
                          "a:65536", "a:", "[::1", "[host]", "[::1]extra"}) {
    assert(!policy::parse_authority(host, authority));
  }
  assert(policy::matches("http://panel.local:00080", "PANEL.LOCAL"));
  assert(!policy::matches("http://panel.local", "panel.local:8080"));
  assert(!policy::matches(std::string("http://panel.local\0evil", 23), "panel.local"));
  assert(!policy::matches("http://" + std::string(513, 'a'), std::string(513, 'a')));
  assert(policy::matches("http://[::1]:8080", "[::1]:8080"));
}

/** New Spotify routes use the actual existing auth-first Origin middleware. */
void check_spotify_routes() {
  for (const char *url : {"/usp/spotify/auth/status", "/usp/spotify/auth/begin",
                          "/usp/spotify/auth/callback", "/usp/spotify/auth/cancel",
                          "/usp/spotify/auth/", "/usp/spotify/auth/page.js",
                          "/usp/spotify/auth/page.css", "/usp/spotify/auth/return.js"}) {
    Harness blocked;
    auto evil = request_for("http://other.invalid", "panel.local", url);
    blocked.invoke(evil);
    assert(blocked.target.calls() == 0 && evil.response_status == 403);
    Harness allowed;
    auto local = request_for("http://panel.local", "panel.local", url);
    allowed.invoke(local);assert(allowed.target.calls() == 3);
    Harness denied;
    auto unauthenticated = request_for("http://panel.local", "panel.local", url);
    unauthenticated.verifier = [](const char *, const char *) {return false;};
    denied.invoke(unauthenticated);
    assert(denied.target.calls() == 0 && unauthenticated.response_status == 401);
    assert(unauthenticated.header_reads == 0);
  }
  std::cout<<"SPOTIFY_AUTH_ORIGIN_NATIVE=PASS routes8 actual_middleware auth_first body_request_upload\n";
}

int main() {
  check_blocked();
  check_compatible();
  check_scope_and_authentication();
  check_parser();
  check_spotify_routes();
  std::cout << "WEB_CUSTOM_ORIGIN_NATIVE=PASS actual middleware request/body/upload auth_first scope clients parser\n";
}
