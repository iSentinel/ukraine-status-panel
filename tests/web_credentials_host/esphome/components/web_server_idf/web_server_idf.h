#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
enum http_method { HTTP_GET, HTTP_POST, HTTP_HEAD, HTTP_OPTIONS, HTTP_PUT, HTTP_PATCH, HTTP_DELETE };
class AsyncWebServerRequest {
 public:
mutable bool auth_rejected_{false};
bool auth_rejected() const { return this->auth_rejected_; }
void mark_auth_rejected() const { this->auth_rejected_ = true; }
  std::function<bool(const char *, const char *)> verifier;
  unsigned challenges{0};
  unsigned auth_calls{0};
  mutable unsigned header_reads{0};
  static constexpr size_t URL_BUF_SIZE = 256;
  std::string url_value{"/"};
  std::string url_to(char (&)[URL_BUF_SIZE]) const { return url_value; }
  std::string origin;
  http_method method_value{HTTP_GET};
  int response_status{0};
  http_method method() const { return method_value; }
  std::optional<std::string> get_header(const char *name) const {
    ++header_reads;
    if (std::string(name) == "Origin" && !origin.empty()) return origin;
    if (std::string(name) == "Host" && !host.empty()) return host;
    return {};
  }
  void send(int status, const char *, const char *) { response_status = status; }
  std::string host{"panel.local"};
  bool authenticate(const char *user, const char *pass) {
    ++auth_calls;
    return this->verifier(user, pass);
  }
  void requestAuthentication() {
  if (this->auth_rejected_) return;
  this->auth_rejected_ = true;
  ++this->challenges; response_status=401;
}
};
class AsyncWebHandler {
 public:
  virtual ~AsyncWebHandler() = default;
  virtual bool canHandle(AsyncWebServerRequest *) const { return true; }
  virtual void handleRequest(AsyncWebServerRequest *) {}
  virtual void handleBody(AsyncWebServerRequest *, uint8_t *, size_t, size_t, size_t) {}
  virtual void handleUpload(AsyncWebServerRequest *, const std::string &, size_t,
                            uint8_t *, size_t, bool) {}
  virtual bool isRequestHandlerTrivial() const { return false; }
};
inline AsyncWebHandler *last_handler = nullptr;
class AsyncWebServer {
 public:
  explicit AsyncWebServer(uint16_t) {}
  void begin() {}
  void end() {}
  void addHandler(AsyncWebHandler *handler) { last_handler = handler; }
};
class DefaultHeaders {
 public:
  static DefaultHeaders &Instance() { static DefaultHeaders instance; return instance; }
  void addHeader(const char *, const char *) {}
};
