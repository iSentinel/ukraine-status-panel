#pragma once
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <cstring>
enum http_method {HTTP_GET,HTTP_POST,HTTP_DELETE};
struct httpd_req_t {const char *uri;std::map<std::string,std::string> headers;};
inline int httpd_resp_set_hdr(httpd_req_t *request,const char *key,const char *value) {
  request->headers[key]=value;return 0;
}
namespace esphome::web_server_idf {
struct AsyncWebServerResponse {const uint8_t *data;size_t size;};
class AsyncWebServerRequest {
 public:
  static constexpr size_t URL_BUF_SIZE=513;
  AsyncWebServerRequest(std::string path,http_method verb,size_t length=0,
                        std::string type="text/plain"):
      path_(std::move(path)),verb_(verb),length_(length),type_(std::move(type)),raw_{path_.c_str(),{}} {}
  http_method method() const {return verb_;}
  size_t contentLength() const {return length_;}
  std::string_view url_to(char (&)[URL_BUF_SIZE]) const {
    return std::string_view(path_).substr(0,path_.find('?'));
  }
  std::optional<std::string> get_header(const char *name) const {
    if(std::strcmp(name,"Content-Type")==0 && !type_.empty())return type_;
    return {};
  }
  operator httpd_req_t *() {return &raw_;}
  void send(int code,const char *,const char *data) {status=code;body=data;}
  AsyncWebServerResponse *beginResponse(int code,const char *type,const uint8_t *data,size_t size) {
    status=code;content_type=type;return new AsyncWebServerResponse{data,size};
  }
  void send(AsyncWebServerResponse *response) {
    body.assign(reinterpret_cast<const char *>(response->data),response->size);delete response;
  }
  std::string content_type;
  int status=0;std::string body;
  const auto &response_headers() const {return raw_.headers;}
 private:
  std::string path_;http_method verb_;size_t length_;std::string type_;httpd_req_t raw_;
};
class AsyncWebHandler {
 public:
  virtual ~AsyncWebHandler() = default;
  virtual bool canHandle(AsyncWebServerRequest *) const {return false;}
  virtual void handleRequest(AsyncWebServerRequest *) {}
  virtual void handleBody(AsyncWebServerRequest *,uint8_t *,size_t,size_t,size_t) {}
  virtual bool isRequestHandlerTrivial() const {return true;}
};
}
