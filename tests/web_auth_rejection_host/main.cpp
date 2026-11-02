#include "web_server_base.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <memory>
#include <vector>

using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_TIMEOUT = -2;
constexpr int HTTPD_SOCK_ERR_TIMEOUT = -3, ESP_ERR_NOT_FOUND = -4;
constexpr int HTTPD_408_REQ_TIMEOUT = 408, HTTPD_400_BAD_REQUEST = 400;
constexpr size_t RECV_CHUNK_SIZE = 1460;
constexpr size_t YIELD_INTERVAL_BYTES = 16 * 1024;
template<class... Args> void fixture_log(Args&&...) {}
constexpr const char *TAG="fixture";
#define ESP_LOGW(...) fixture_log(__VA_ARGS__)
#define ESP_LOGE(...) fixture_log(__VA_ARGS__)
#define ESP_LOGV(...) fixture_log(__VA_ARGS__)
unsigned receive_calls = 0, yields = 0;
std::string fixture_body;
constexpr int HTTPD_404_NOT_FOUND = 404;
int fixture_fail_after=-1, fixture_flip_after=-1;
int httpd_req_recv(httpd_req_t *req, char *buffer, size_t count) {
  ++receive_calls;
  if (int(receive_calls)==fixture_flip_after) fixture_authenticated=true;
  if (int(receive_calls)==fixture_fail_after) return HTTPD_SOCK_ERR_TIMEOUT;
  if (fixture_body.empty()) std::fill(buffer, buffer+count, 'x');
  else std::copy_n(fixture_body.data()+req->consumed,count,buffer);
  req->consumed += count;
  return int(count);
}
void httpd_resp_send_err(httpd_req_t *, int status, const char *) {
  fixture_responses.push_back(status);
}
void vTaskDelay(int) { ++yields; }
#include "multipart.h"
using esphome::web_server_idf::MultipartReader;
using esphome::web_server_idf::parse_multipart_boundary;
#include "combined-production.inc"

using namespace esphome::web_server_base::internal;
struct Target : AsyncWebHandler {
  unsigned bodies = 0, requests = 0;
  void handleBody(AsyncWebServerRequest *, uint8_t *, size_t,
                  size_t, size_t) override { ++bodies; }
  void handleRequest(AsyncWebServerRequest *) override { ++requests; }
};


struct UploadTarget : Target {
  unsigned upload_calls=0, starts=0, ends=0;
  size_t bytes=0;
  void handleUpload(AsyncWebServerRequest *,const std::string &,size_t index,
                    uint8_t *data,size_t len,bool final) override {
    ++upload_calls;
    if (index==0 && len==0 && !final) ++starts;
    if (final) ++ends;
    if (len) {
      assert(index==bytes);
      for (size_t i=0;i<len;++i) assert(data[i]=='x');
      bytes+=len;
    }
  }
};
struct Result {
  int code;
  unsigned bodies, requests;
  size_t consumed;
};

/** Drive actual middleware and receiver with explicit synthetic I/O cases. */
static Result run_case(bool authenticated, const char *origin,
                       int fail_after = -1, int flip_after = -1,
                       size_t total = RECV_CHUNK_SIZE * 3 + 17) {
  fixture_authenticated = authenticated;
  fixture_origin = origin;
  fixture_responses.clear();
  receive_calls = 0;
  fixture_fail_after = fail_after;
  fixture_flip_after = flip_after;
  Credentials credentials;
  credentials.set_username("admin");
  credentials.set_password("synthetic-fixture");
  Target target;
  AuthMiddlewareHandler middleware(&target, &credentials);
  AsyncWebServer server(80);
  server.addHandler(&middleware);
  httpd_req_t request{total, 0};
  const int code = server.handle_raw_body_(&request, "application/json");
  return {code, target.bodies, target.requests, request.consumed};
}

/** Every rejected body produces one response and never reaches the app. */
static void rejection_cases() {
  auto denied = run_case(false, "");
  assert(denied.code == ESP_OK && denied.consumed == 4397);
  assert(denied.bodies == 0 && denied.requests == 0);
  assert(fixture_responses == std::vector<int>{401});
  auto changed = run_case(false, "", -1, 2);
  assert(changed.bodies == 0 && changed.requests == 0);
  assert(fixture_responses == std::vector<int>{401});
  auto origin = run_case(true, "https://foreign.invalid");
  assert(origin.bodies == 0 && origin.requests == 0);
  assert(fixture_responses == std::vector<int>{403});
  auto empty = run_case(false, "", -1, -1, 0);
  assert(empty.bodies == 0 && empty.requests == 0);
  assert(fixture_responses == std::vector<int>{401});
}

/** A later receive failure closes the request without a second response. */
static void receive_error_cases() {
  auto late = run_case(false, "", 2);
  assert(late.code == ESP_ERR_TIMEOUT && late.consumed == RECV_CHUNK_SIZE);
  assert(late.bodies == 0 && late.requests == 0);
  assert(fixture_responses == std::vector<int>{401});
  auto early = run_case(true, "", 1);
  assert(early.code == ESP_ERR_TIMEOUT && early.consumed == 0);
  assert(fixture_responses == std::vector<int>{408});
}

/** Valid fragments still reach the same handler and one final dispatch. */
static void valid_case() {
  auto accepted = run_case(true, "");
  assert(accepted.code == ESP_OK && accepted.consumed == 4397);
  assert(accepted.bodies == 4 && accepted.requests == 1);
  assert(fixture_responses.empty());
}

/** Explicit reuse of the same storage must start a fresh request state. */
static void reused_address_case() {
  fixture_responses.clear();
  fixture_authenticated = false;
  fixture_origin.clear();
  alignas(AsyncWebServerRequest) unsigned char bytes[sizeof(AsyncWebServerRequest)];
  auto *first = new (bytes) AsyncWebServerRequest(nullptr);
  first->requestAuthentication();
  assert(first->auth_rejected());
  first->~AsyncWebServerRequest();
  fixture_authenticated = true;
  auto *second = new (bytes) AsyncWebServerRequest(nullptr);
  assert(!second->auth_rejected());
  assert(second->authenticate("admin", "synthetic-fixture"));
  second->~AsyncWebServerRequest();
  assert(fixture_responses == std::vector<int>{401});
}

/** Run the real multipart reader and server loop with synthetic file bytes. */
static Result run_multipart(UploadTarget &target, bool authenticated,
                           const char *origin, const std::string &body,
                           int failure = -1,
                           const char *type = "multipart/form-data; boundary=fixture") {
  fixture_authenticated = authenticated;
  fixture_origin = origin;
  fixture_responses.clear();
  fixture_body = body;
  fixture_fail_after = failure;
  fixture_flip_after = -1;
  receive_calls = 0;
  Credentials credentials;
  credentials.set_username("admin");
  credentials.set_password("synthetic-fixture");
  AuthMiddlewareHandler middleware(&target, &credentials);
  AsyncWebServer server(80);
  server.addHandler(&middleware);
  httpd_req_t request{body.size(), 0};
  const int code = server.handle_multipart_upload_(&request, type);
  fixture_body.clear();
  return {code, target.bodies, target.requests, request.consumed};
}

/** Build a bounded multipart payload without any real firmware image. */
static std::string multipart_body(bool malformed = false) {
  const std::string header =
      "--fixture\r\nContent-Disposition: form-data; name=\"file\";"
      " filename=\"synthetic.bin\"\r\n"
      "Content-Type: application/octet-stream\r\n\r\n";
  return header + std::string(4000, 'x') +
      (malformed ? "\r\n--fixture\r\n\x01" : "\r\n--fixture--\r\n");
}

/** Denied uploads are drained once, with no protected upload callbacks. */
static void denied_multipart_cases() {
  for (bool foreign_origin : {false, true}) {
    UploadTarget target;
    const auto body = multipart_body();
    const auto result = run_multipart(target, foreign_origin,
        foreign_origin ? "https://foreign.invalid" : "", body);
    assert(result.code == ESP_OK && result.consumed == body.size());
    assert(target.upload_calls == 0 && target.requests == 0);
    assert(fixture_responses == std::vector<int>{foreign_origin ? 403 : 401});
  }
  UploadTarget empty;
  const auto result = run_multipart(empty, false, "", "");
  assert(result.code == ESP_OK && empty.upload_calls == 0);
  assert(fixture_responses == std::vector<int>{401});
}

/** Receiving or parsing failure after a denial must not emit 408 or 400. */
static void denied_multipart_error_cases() {
  UploadTarget timeout;
  auto result = run_multipart(timeout, false, "", multipart_body(), 2);
  assert(result.code == ESP_ERR_TIMEOUT && timeout.upload_calls == 0);
  assert(fixture_responses == std::vector<int>{401});
  UploadTarget malformed;
  result = run_multipart(malformed, false, "", multipart_body(true));
  assert(result.code == ESP_FAIL && malformed.upload_calls == 0);
  assert(fixture_responses == std::vector<int>{401});
}

/** Authorized uploads retain all bytes, start/end notifications and dispatch. */
static void valid_multipart_and_initial_error_cases() {
  UploadTarget valid;
  auto result = run_multipart(valid, true, "", multipart_body());
  assert(result.code == ESP_OK && valid.bytes == 4000);
  assert(valid.starts == 1 && valid.ends == 1 && valid.requests == 1);
  assert(fixture_responses.empty());
  UploadTarget early;
  result = run_multipart(early, true, "", multipart_body(), 1);
  assert(result.code == ESP_ERR_TIMEOUT && early.upload_calls == 0);
  assert(fixture_responses == std::vector<int>{408});
  UploadTarget bad_boundary;
  result = run_multipart(bad_boundary, true, "", multipart_body(), -1,
                        "multipart/form-data");
  assert(result.code == ESP_FAIL && bad_boundary.upload_calls == 0);
  assert(fixture_responses == std::vector<int>{400});
}

int main() {
  rejection_cases();
  receive_error_cases();
  valid_case();
  reused_address_case();
  denied_multipart_cases();
  denied_multipart_error_cases();
  valid_multipart_and_initial_error_cases();
  std::cout << "WEB_AUTH_REJECTION=PASS raw8 multipart8 actual_reader_and_parser"
               " single401 single403 drained no_protected_denied_calls"
               " late_errors_suppressed valid_upload4000 no_device\n";
}
