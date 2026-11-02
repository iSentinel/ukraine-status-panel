#ifdef USE_ESP32

#include <cstdarg>
#include <memory>
#include <cstring>
#include <cctype>
#include <cinttypes>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include "esp_tls_crypto.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "utils.h"
#include "web_server_idf.h"

#ifdef USE_WEBSERVER_AUTH_DIGEST
#include <esp_random.h>
#include <esp_rom_md5.h>
#include "digest_auth_state.h"
#endif

#ifdef USE_WEBSERVER_OTA
#include <multipart_parser.h>
#include "multipart.h"  // For parse_multipart_boundary and other utils
#endif

#ifdef USE_WEBSERVER
#include "esphome/components/web_server/web_server.h"
#include "esphome/components/web_server/list_entities.h"
#endif  // USE_WEBSERVER

// Include socket headers after Arduino headers to avoid IPADDR_NONE/INADDR_NONE macro conflicts
#include <cerrno>
#include <sys/socket.h>

namespace esphome::web_server_idf {

// Status strings not provided by esp_http_server.h
#ifndef HTTPD_401
#define HTTPD_401 "401 Unauthorized"
#endif
#ifndef HTTPD_409
#define HTTPD_409 "409 Conflict"
#endif
#ifndef HTTPD_422
#define HTTPD_422 "422 Unprocessable Entity"
#endif

#define CRLF_STR "\r\n"
#define CRLF_LEN (sizeof(CRLF_STR) - 1)

static const char *const TAG = "web_server_idf";

// Chunk size for streaming request bodies; matches the Arduino AsyncWebServer buffer size.
// Buffers of this size must live on the heap - the httpd task stack is too small.
static constexpr size_t RECV_CHUNK_SIZE = 1460;
static constexpr size_t YIELD_INTERVAL_BYTES = 16 * 1024;  // Yield every 16KB to prevent watchdog

// Global instance to avoid guard variable (saves 8 bytes)
// This is initialized at program startup before any threads
namespace {
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
DefaultHeaders default_headers_instance;
}  // namespace

DefaultHeaders &DefaultHeaders::Instance() { return default_headers_instance; }

namespace {
// Non-blocking send function to prevent watchdog timeouts when TCP buffers are full
/**
 * Sends data on a socket in non-blocking mode.
 *
 * @param hd      HTTP server handle (unused).
 * @param sockfd  Socket file descriptor.
 * @param buf     Buffer to send.
 * @param buf_len Length of buffer.
 * @param flags   Flags for send().
 * @return
 *   - Number of bytes sent on success.
 *   - HTTPD_SOCK_ERR_INVALID if buf is nullptr.
 *   - HTTPD_SOCK_ERR_TIMEOUT if the send buffer is full (EAGAIN/EWOULDBLOCK).
 *   - HTTPD_SOCK_ERR_FAIL for other errors.
 */
[[maybe_unused]] int nonblocking_send(httpd_handle_t hd, int sockfd, const char *buf, size_t buf_len, int flags) {
  if (buf == nullptr) {
    return HTTPD_SOCK_ERR_INVALID;
  }

  // Use MSG_DONTWAIT to prevent blocking when TCP send buffer is full
  int ret = send(sockfd, buf, buf_len, flags | MSG_DONTWAIT);
  if (ret < 0) {
    const int err = errno;
    if (err == EAGAIN || err == EWOULDBLOCK) {
      // Buffer full - retry later
      return HTTPD_SOCK_ERR_TIMEOUT;
    }
    // Real error
    ESP_LOGD(TAG, "send error: errno %d", err);
    return HTTPD_SOCK_ERR_FAIL;
  }
  return ret;
}
}  // namespace

void AsyncWebServer::safe_close_with_shutdown(httpd_handle_t hd, int sockfd) {
  // CRITICAL: Shut down receive BEFORE closing to prevent lwIP race conditions
  //
  // The race condition occurs because close() initiates lwIP teardown while
  // the TCP/IP thread can still receive packets, causing assertions when
  // recv_tcp() sees partially-torn-down state.
  //
  // By shutting down receive first, we tell lwIP to stop accepting new data BEFORE
  // the teardown begins, eliminating the race window. We only shutdown RD (not RDWR)
  // to allow the FIN packet to be sent cleanly during close().
  //
  // Note: This function may be called with an already-closed socket if the network
  // stack closed it. In that case, shutdown() will fail but close() is safe to call.
  //
  // See: https://github.com/esphome/esphome-webserver/issues/163

  // Attempt shutdown - ignore errors as socket may already be closed
  shutdown(sockfd, SHUT_RD);

  // Always close - safe even if socket is already closed by network stack
  close(sockfd);
}

void AsyncWebServer::end() {
  if (this->server_) {
    httpd_stop(this->server_);
    this->server_ = nullptr;
  }
}

void AsyncWebServer::begin() {
  if (this->server_) {
    this->end();
  }
  // Default httpd stack is defined by ESP-IDF. Increase to accommodate SerializationBuffer's
  // 640-byte stack buffer used by web_server JSON request handlers.
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = config.stack_size + 256;
  config.server_port = this->port_;
  config.uri_match_fn = [](const char * /*unused*/, const char * /*unused*/, size_t /*unused*/) { return true; };
  // Always enable LRU purging to handle socket exhaustion gracefully.
  // When max sockets is reached, the oldest connection is closed to make room for new ones.
  // This prevents "httpd_accept_conn: error in accept (23)" errors.
  // See: https://github.com/esphome/esphome/issues/12464
  config.lru_purge_enable = true;
  // Use custom close function that shuts down before closing to prevent lwIP race conditions
  config.close_fn = AsyncWebServer::safe_close_with_shutdown;
  if (httpd_start(&this->server_, &config) == ESP_OK) {
    const httpd_uri_t handler_get = {
        .uri = "",
        .method = HTTP_GET,
        .handler = AsyncWebServer::request_handler,
        .user_ctx = this,
    };
    httpd_register_uri_handler(this->server_, &handler_get);

    const httpd_uri_t handler_post = {
        .uri = "",
        .method = HTTP_POST,
        .handler = AsyncWebServer::request_post_handler,
        .user_ctx = this,
    };
    httpd_register_uri_handler(this->server_, &handler_post);

    const httpd_uri_t handler_options = {
        .uri = "",
        .method = HTTP_OPTIONS,
        .handler = AsyncWebServer::request_handler,
        .user_ctx = this,
    };
    httpd_register_uri_handler(this->server_, &handler_options);
  }
}

esp_err_t AsyncWebServer::request_post_handler(httpd_req_t *r) {
  ESP_LOGVV(TAG, "Enter AsyncWebServer::request_post_handler. uri=%s", r->uri);
  auto content_type = request_get_header(r, "Content-Type");

  if (!request_has_header(r, "Content-Length")) {
    ESP_LOGW(TAG, "Content length is required for post: %s", r->uri);
    httpd_resp_send_err(r, HTTPD_411_LENGTH_REQUIRED, nullptr);
    return ESP_OK;
  }

  if (content_type.has_value()) {
    const char *content_type_char = content_type.value().c_str();

    // Check most common case first
    size_t content_type_len = strlen(content_type_char);
    if (strcasestr_n(content_type_char, content_type_len, "application/x-www-form-urlencoded") != nullptr) {
      // Normal form data - proceed with regular handling
#ifdef USE_WEBSERVER_OTA
    } else if (strcasestr_n(content_type_char, content_type_len, "multipart/form-data") != nullptr) {
      auto *server = static_cast<AsyncWebServer *>(r->user_ctx);
      return server->handle_multipart_upload_(r, content_type_char);
#endif
    } else {
      // Other content types (e.g. application/json) are delivered raw to a matching
      // custom handler via handleBody(), like the Arduino AsyncWebServer does
      auto *server = static_cast<AsyncWebServer *>(r->user_ctx);
      return server->handle_raw_body_(r, content_type_char);
    }
  }

  // Handle regular form data
  if (r->content_len > CONFIG_HTTPD_MAX_REQ_HDR_LEN) {
    ESP_LOGW(TAG, "Request size is to big: %zu", r->content_len);
    httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, nullptr);
    return ESP_FAIL;
  }

  std::string post_query;
  if (r->content_len > 0) {
    post_query.resize(r->content_len);
    const int ret = httpd_req_recv(r, &post_query[0], r->content_len + 1);
    if (ret <= 0) {  // 0 return value indicates connection closed
      if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
        httpd_resp_send_err(r, HTTPD_408_REQ_TIMEOUT, nullptr);
        return ESP_ERR_TIMEOUT;
      }
      httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, nullptr);
      return ESP_FAIL;
    }
  }

  AsyncWebServerRequest req(r, std::move(post_query));
  return static_cast<AsyncWebServer *>(r->user_ctx)->request_handler_(&req);
}

esp_err_t AsyncWebServer::request_handler(httpd_req_t *r) {
  ESP_LOGVV(TAG, "Enter AsyncWebServer::request_handler. method=%u, uri=%s", r->method, r->uri);
  AsyncWebServerRequest req(r);
  return static_cast<AsyncWebServer *>(r->user_ctx)->request_handler_(&req);
}

esp_err_t AsyncWebServer::request_handler_(AsyncWebServerRequest *request) const {
  for (auto *handler : this->handlers_) {
    if (handler->canHandle(request)) {
      // At now process only basic requests.
      // OTA requires multipart request support and handleUpload for it
      handler->handleRequest(request);
      return ESP_OK;
    }
  }
  if (this->on_not_found_) {
    this->on_not_found_(request);
    return ESP_OK;
  }
  return ESP_ERR_NOT_FOUND;
}

esp_err_t AsyncWebServer::handle_raw_body_(httpd_req_t *r, const char *content_type) {
  AsyncWebServerRequest req(r);
  AsyncWebHandler *handler = nullptr;
  for (auto *h : this->handlers_) {
    if (h->canHandle(&req)) {
      handler = h;
      break;
    }
  }

  if (handler == nullptr) {
    ESP_LOGW(TAG, "Unsupported content type for POST: %s", content_type);
    // fallback to get handler to support backward compatibility
    return this->request_handler_(&req);
  }

  const size_t total = r->content_len;
  if (total > 0) {
    auto buffer = std::make_unique_for_overwrite<char[]>(RECV_CHUNK_SIZE);
    size_t bytes_since_yield = 0;

    for (size_t index = 0; index < total;) {
      int recv_len = httpd_req_recv(r, buffer.get(), std::min(total - index, RECV_CHUNK_SIZE));

      if (recv_len <= 0) {
#ifdef USE_WEBSERVER_AUTH
        if (!req.auth_rejected())
#endif
        httpd_resp_send_err(r, recv_len == HTTPD_SOCK_ERR_TIMEOUT ? HTTPD_408_REQ_TIMEOUT : HTTPD_400_BAD_REQUEST,
                            nullptr);
        return recv_len == HTTPD_SOCK_ERR_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_FAIL;
      }

      handler->handleBody(&req, reinterpret_cast<uint8_t *>(buffer.get()), recv_len, index, total);
      index += recv_len;
      bytes_since_yield += recv_len;

      if (bytes_since_yield > YIELD_INTERVAL_BYTES) {
        vTaskDelay(1);
        bytes_since_yield = 0;
      }
    }
  }

  handler->handleRequest(&req);
  return ESP_OK;
}

AsyncWebServerRequest::~AsyncWebServerRequest() {
  delete this->rsp_;
  for (auto *param : this->params_) {
    delete param;  // NOLINT(cppcoreguidelines-owning-memory)
  }
}

bool AsyncWebServerRequest::hasHeader(const char *name) const { return request_has_header(*this, name); }

optional<std::string> AsyncWebServerRequest::get_header(const char *name) const {
  return request_get_header(*this, name);
}

StringRef AsyncWebServerRequest::url_to(std::span<char, URL_BUF_SIZE> buffer) const {
  const char *uri = this->req_->uri;
  const char *query_start = strchr(uri, '?');
  size_t uri_len = query_start ? static_cast<size_t>(query_start - uri) : strlen(uri);
  size_t copy_len = std::min(uri_len, URL_BUF_SIZE - 1);
  memcpy(buffer.data(), uri, copy_len);
  buffer[copy_len] = '\0';
  // Decode URL-encoded characters in-place (e.g., %20 -> space)
  size_t decoded_len = url_decode(buffer.data());
  return StringRef(buffer.data(), decoded_len);
}

void AsyncWebServerRequest::redirect(const std::string &url) {
  httpd_resp_set_status(*this, "302 Found");
  httpd_resp_set_hdr(*this, "Location", url.c_str());
  httpd_resp_set_hdr(*this, "Connection", "close");
  httpd_resp_send(*this, nullptr, 0);
}

void AsyncWebServerRequest::init_response_(AsyncWebServerResponse *rsp, int code, const char *content_type) {
  // Set status code - use constants for common codes, default to 500 for unknown codes
  const char *status;
  switch (code) {
    case 200:
      status = HTTPD_200;
      break;
    case 202:
      status = "202 Accepted";
      break;
    case 204:
      status = HTTPD_204;
      break;
    case 400:
      status = HTTPD_400;
      break;
    case 401:
      status = HTTPD_401;
      break;
    case 403:
      status = "403 Forbidden";
      break;
    case 404:
      status = HTTPD_404;
      break;
    case 405:
      status = "405 Method Not Allowed";
      break;
    case 409:
      status = HTTPD_409;
      break;
    case 422:
      status = HTTPD_422;
      break;
    case 503:
      status = "503 Service Unavailable";
      break;
    default:
      status = HTTPD_500;
      break;
  }
  httpd_resp_set_status(*this, status);

  if (content_type && *content_type) {
    httpd_resp_set_type(*this, content_type);
  }
  httpd_resp_set_hdr(*this, "Accept-Ranges", "none");

  for (const auto &header : DefaultHeaders::Instance().headers_) {
    httpd_resp_set_hdr(*this, header.name, header.value);
  }

  delete this->rsp_;
  this->rsp_ = rsp;
}

#ifdef USE_WEBSERVER_AUTH

#ifdef USE_WEBSERVER_AUTH_DIGEST
namespace {

// Extract the value of a Digest auth parameter (e.g. "nonce") from the comma-separated
// parameter list. Values may be quoted or bare. Returns an empty ref when the key is absent.
// Only whole parameter names match, so "nc" does not match inside "cnonce".
StringRef digest_param(StringRef params, const char *key) {
  size_t key_len = strlen(key);
  const char *base = params.c_str();
  size_t n = params.size();
  size_t i = 0;
  while (i < n) {
    while (i < n && (base[i] == ' ' || base[i] == ','))
      i++;
    size_t name_start = i;
    while (i < n && base[i] != '=' && base[i] != ',')
      i++;
    if (i >= n || base[i] == ',')
      continue;  // token without a '=', skip it
    size_t name_len = i - name_start;
    while (name_len > 0 && base[name_start + name_len - 1] == ' ')
      name_len--;
    i++;  // consume '='
    const char *val_start;
    size_t val_len;
    if (i < n && base[i] == '"') {
      i++;
      val_start = base + i;
      while (i < n && base[i] != '"')
        i++;
      val_len = (base + i) - val_start;
      if (i < n)
        i++;  // consume closing quote
    } else {
      val_start = base + i;
      while (i < n && base[i] != ',')
        i++;
      val_len = (base + i) - val_start;
    }
    if (name_len == key_len && memcmp(base + name_start, key, key_len) == 0)
      return StringRef(val_start, val_len);
    while (i < n && base[i] != ',')
      i++;
  }
  return StringRef();
}

static constexpr const char *DIGEST_REALM = "Login Required";
static constexpr const char *DIGEST_QOP = "auth";

// Bounded server-side challenge state. The store itself is allocation-free;
// the mutex only serializes the httpd task(s) touching replay state.
digest_auth::NonceStore digest_nonce_store;

Mutex &digest_nonce_mutex() {
  static Mutex mutex;
  return mutex;
}

void remember_digest_nonce(const char *nonce, const char *opaque, uint32_t now) {
  LockGuard lock(digest_nonce_mutex());
  digest_nonce_store.issue(nonce, opaque, now);
}

digest_auth::NonceStatus consume_digest_nonce(StringRef nonce, StringRef opaque, uint32_t nc, uint32_t now) {
  LockGuard lock(digest_nonce_mutex());
  return digest_nonce_store.consume_result(
      std::string_view(nonce.c_str(), nonce.size()),
      std::string_view(opaque.c_str(), opaque.size()), nc, now);
}

struct DigestFields {
  StringRef nonce;
  StringRef nc;
  StringRef cnonce;
  StringRef opaque;
  StringRef response;
  uint32_t nc_value{};
};

bool parse_digest_fields(StringRef params, const char *username,
                         const char *request_uri, DigestFields &fields) {
  if (request_uri == nullptr || digest_param(params, "username") != username)
    return false;
  const StringRef realm = digest_param(params, "realm");
  const StringRef uri = digest_param(params, "uri");
  const StringRef qop = digest_param(params, "qop");
  fields.nonce = digest_param(params, "nonce");
  fields.nc = digest_param(params, "nc");
  fields.cnonce = digest_param(params, "cnonce");
  fields.opaque = digest_param(params, "opaque");
  fields.response = digest_param(params, "response");
  if (realm != DIGEST_REALM || qop != DIGEST_QOP || uri != request_uri)
    return false;
  if (fields.nonce.size() != digest_auth::TOKEN_HEX_LENGTH ||
      fields.opaque.size() != digest_auth::TOKEN_HEX_LENGTH ||
      fields.cnonce.size() == 0 || fields.response.size() != 32)
    return false;
  return digest_auth::parse_nonce_count(
      std::string_view(fields.nc.c_str(), fields.nc.size()), fields.nc_value);
}

void digest_ha1(const char *username, const char *password, char *out) {
  md5_context_t ctx;
  uint8_t digest[16];
  esp_rom_md5_init(&ctx);
  esp_rom_md5_update(&ctx, username, strlen(username));
  esp_rom_md5_update(&ctx, ":", 1);
  esp_rom_md5_update(&ctx, DIGEST_REALM, strlen(DIGEST_REALM));
  esp_rom_md5_update(&ctx, ":", 1);
  esp_rom_md5_update(&ctx, password, strlen(password));
  esp_rom_md5_final(digest, &ctx);
  format_hex_to(out, 33, digest, sizeof(digest));
}

void digest_ha2(const char *method, const char *request_uri, char *out) {
  md5_context_t ctx;
  uint8_t digest[16];
  esp_rom_md5_init(&ctx);
  esp_rom_md5_update(&ctx, method, strlen(method));
  esp_rom_md5_update(&ctx, ":", 1);
  esp_rom_md5_update(&ctx, request_uri, strlen(request_uri));
  esp_rom_md5_final(digest, &ctx);
  format_hex_to(out, 33, digest, sizeof(digest));
}

void digest_expected(const char *ha1, const char *ha2,
                     const DigestFields &fields, char *out) {
  md5_context_t ctx;
  uint8_t digest[16];
  esp_rom_md5_init(&ctx);
  esp_rom_md5_update(&ctx, ha1, 32);
  esp_rom_md5_update(&ctx, ":", 1);
  esp_rom_md5_update(&ctx, fields.nonce.c_str(), fields.nonce.size());
  esp_rom_md5_update(&ctx, ":", 1);
  esp_rom_md5_update(&ctx, fields.nc.c_str(), fields.nc.size());
  esp_rom_md5_update(&ctx, ":", 1);
  esp_rom_md5_update(&ctx, fields.cnonce.c_str(), fields.cnonce.size());
  esp_rom_md5_update(&ctx, ":", 1);
  esp_rom_md5_update(&ctx, DIGEST_QOP, strlen(DIGEST_QOP));
  esp_rom_md5_update(&ctx, ":", 1);
  esp_rom_md5_update(&ctx, ha2, 32);
  esp_rom_md5_final(digest, &ctx);
  format_hex_to(out, 33, digest, sizeof(digest));
}

bool digest_equal(const char *expected, StringRef response) {
  uint8_t result = 0;
  for (size_t i = 0; i < 32; i++)
    result |= static_cast<uint8_t>(expected[i] ^ response[i]);
  return result == 0;
}

// Verify Digest auth against the server-issued challenge and actual request target.
bool check_digest_auth(const char *username, const char *password,
                       const std::string &header, const char *method,
                       const char *request_uri,
                       std::array<char, 33> &verified_response,
                       bool *stale_response = nullptr) {
  if (stale_response != nullptr) *stale_response = false;
  const size_t prefix_len = sizeof("Digest ") - 1;
  const StringRef params(header.c_str() + prefix_len, header.size() - prefix_len);
  DigestFields fields;
  if (!parse_digest_fields(params, username, request_uri, fields))
    return false;
  char ha1[33];
  char ha2[33];
  char expected[33];
  digest_ha1(username, password, ha1);
  digest_ha2(method, request_uri, ha2);
  digest_expected(ha1, ha2, fields, expected);
  if (!digest_equal(expected, fields.response))
    return false;
  // A body callback may already have admitted this exact signed request.
  // A new HTTP request starts with an empty cache and must consume its own nc.
  if (digest_equal(verified_response.data(), fields.response))
    return true;
  const auto nonce_status = consume_digest_nonce(
      fields.nonce, fields.opaque, fields.nc_value, millis());
  if (nonce_status != digest_auth::NonceStatus::ACCEPTED) {
    if (stale_response != nullptr)
      *stale_response = nonce_status == digest_auth::NonceStatus::EXPIRED ||
                        nonce_status == digest_auth::NonceStatus::UNKNOWN;
    return false;
  }
  std::memcpy(verified_response.data(), expected, verified_response.size());
  return true;
}

}  // namespace
#endif  // USE_WEBSERVER_AUTH_DIGEST

bool AsyncWebServerRequest::authenticate(const char *username, const char *password) const {
  if (this->auth_rejected_) return false;
  if (username == nullptr || password == nullptr || *username == 0) {
    return true;
  }
  auto auth = this->get_header("Authorization");
  if (!auth.has_value()) {
    return false;
  }

  auto *auth_str = auth.value().c_str();

#ifdef USE_WEBSERVER_AUTH_DIGEST
  // The build fixed the scheme to Digest, so the Basic path is compiled out entirely.
  const auto auth_prefix_len = sizeof("Digest ") - 1;
  if (strncmp("Digest ", auth_str, auth_prefix_len) != 0) {
    ESP_LOGW(TAG, "Only Digest authorization supported");
    return false;
  }
  return check_digest_auth(username, password, auth.value(),
                           http_method_str(this->method()), this->req_->uri,
                           this->digest_verified_response_, &this->digest_stale_response_);
#else
  const auto auth_prefix_len = sizeof("Basic ") - 1;
  if (strncmp("Basic ", auth_str, auth_prefix_len) != 0) {
    ESP_LOGW(TAG, "Only Basic authorization supported");
    return false;
  }

  // Build user:pass in stack buffer to avoid heap allocation
  constexpr size_t max_user_info_len = 256;
  char user_info[max_user_info_len];
  size_t user_len = strlen(username);
  size_t pass_len = strlen(password);
  size_t user_info_len = user_len + 1 + pass_len;

  if (user_info_len >= max_user_info_len) {
    ESP_LOGW(TAG, "Credentials too long for authentication");
    return false;
  }

  memcpy(user_info, username, user_len);
  user_info[user_len] = ':';
  memcpy(user_info + user_len + 1, password, pass_len);
  user_info[user_info_len] = '\0';

  // Base64 output size is ceil(input_len * 4/3) + 1, with input bounded to 256 bytes
  // max output is ceil(256 * 4/3) + 1 = 343 bytes, use 350 for safety
  constexpr size_t max_digest_len = 350;
  char digest[max_digest_len];
  size_t out;
  esp_crypto_base64_encode(reinterpret_cast<uint8_t *>(digest), max_digest_len, &out,
                           reinterpret_cast<const uint8_t *>(user_info), user_info_len);

  // Constant-time comparison to avoid timing side channels.
  // No early return on length mismatch — the length difference is folded
  // into the accumulator so any mismatch is rejected.
  const char *provided = auth_str + auth_prefix_len;
  size_t digest_len = out;  // length from esp_crypto_base64_encode
  // Derive provided_len from the already-sized std::string rather than
  // rescanning with strlen (avoids attacker-controlled scan length).
  size_t provided_len = auth.value().size() - auth_prefix_len;
  // Use full-width XOR so any bit difference in the lengths is preserved
  // (uint8_t truncation would miss differences in higher bytes, e.g.
  // digest_len vs digest_len + 256).
  volatile size_t result = digest_len ^ provided_len;
  // Iterate over the expected digest length only — the full-width length
  // XOR above already rejects any length mismatch, and bounding the loop
  // prevents a long Authorization header from forcing extra work.
  for (size_t i = 0; i < digest_len; i++) {
    char provided_ch = (i < provided_len) ? provided[i] : 0;
    result |= static_cast<uint8_t>(digest[i] ^ provided_ch);
  }
  return result == 0;
#endif  // USE_WEBSERVER_AUTH_DIGEST
}

void AsyncWebServerRequest::requestAuthentication() const {
  if (this->auth_rejected_) return;
  this->auth_rejected_ = true;
  httpd_resp_set_hdr(*this, "Connection", "keep-alive");
#ifdef USE_WEBSERVER_AUTH_DIGEST
  // Issue and remember a bounded random challenge before sending it.
  // The header buffer stays alive until httpd_resp_send_err() sends the response.
  uint8_t random_bytes[16];
  char nonce[33];
  char opaque[33];
  char header[160];
  esp_fill_random(random_bytes, sizeof(random_bytes));
  format_hex_to(nonce, random_bytes, sizeof(random_bytes));
  esp_fill_random(random_bytes, sizeof(random_bytes));
  format_hex_to(opaque, random_bytes, sizeof(random_bytes));
  remember_digest_nonce(nonce, opaque, millis());
  snprintf(header, sizeof(header),
           R"(Digest realm="%s", qop="%s", nonce="%s", opaque="%s"%s)",
           DIGEST_REALM, DIGEST_QOP, nonce, opaque,
           this->digest_stale_response_ ? ", stale=true" : "");
  httpd_resp_set_hdr(*this, "WWW-Authenticate", header);
#else
  httpd_resp_set_hdr(*this, "WWW-Authenticate", "Basic realm=\"Login Required\"");
#endif  // USE_WEBSERVER_AUTH_DIGEST
  httpd_resp_send_err(*this, HTTPD_401_UNAUTHORIZED, nullptr);
}
#endif  // USE_WEBSERVER_AUTH

AsyncWebParameter *AsyncWebServerRequest::getParam(const char *name) {
  // Check cache first - only successful lookups are cached
  for (auto *param : this->params_) {
    if (param->name() == name) {
      return param;
    }
  }

  // Look up value from query strings
  auto val = this->find_query_value_(name);

  // Don't cache misses to avoid wasting memory when handlers check for
  // optional parameters that don't exist in the request
  if (!val.has_value()) {
    return nullptr;
  }

  auto *param = new AsyncWebParameter(name, val.value());  // NOLINT(cppcoreguidelines-owning-memory)
  this->params_.push_back(param);
  return param;
}

/// Search post_query then URL query with a callback.
/// Returns first truthy result, or value-initialized default.
/// URL query is accessed directly from req->uri (same pattern as url_to()).
template<typename Func>
static auto search_query_sources(httpd_req_t *req, const std::string &post_query, const char *name, Func func)
    -> decltype(func(nullptr, size_t{0}, name)) {
  if (!post_query.empty()) {
    auto result = func(post_query.c_str(), post_query.size(), name);
    if (result) {
      return result;
    }
  }
  // Use httpd API for query length, then access string directly from URI.
  // http_parser identifies components by offset/length without modifying the URI string.
  // This is the same pattern used by url_to().
  auto len = httpd_req_get_url_query_len(req);
  if (len == 0) {
    return {};
  }
  const char *query = strchr(req->uri, '?');
  if (query == nullptr) {
    return {};
  }
  query++;  // skip '?'
  return func(query, len, name);
}

optional<std::string> AsyncWebServerRequest::find_query_value_(const char *name) const {
  return search_query_sources(this->req_, this->post_query_, name,
                              [](const char *q, size_t len, const char *k) { return query_key_value(q, len, k); });
}

bool AsyncWebServerRequest::hasArg(const char *name) {
  return search_query_sources(this->req_, this->post_query_, name, query_has_key);
}

std::string AsyncWebServerRequest::arg(const char *name) {
  auto val = this->find_query_value_(name);
  if (val.has_value()) {
    return std::move(val.value());
  }
  return {};
}

void AsyncWebServerResponse::addHeader(const char *name, const char *value) {
  httpd_resp_set_hdr(*this->req_, name, value);
}

void AsyncResponseStream::print(float value) {
  // Use stack buffer to avoid temporary string allocation
  // Size: sign (1) + digits (10) + decimal (1) + precision (6) + exponent (5) + null (1) = 24, use 32 for safety
  char buf[32];
  int len = snprintf(buf, sizeof(buf), "%f", value);
  this->content_.append(buf, len);
}

void AsyncResponseStream::printf(const char *fmt, ...) {
  va_list args;

  va_start(args, fmt);
  const int length = vsnprintf(nullptr, 0, fmt, args);
  va_end(args);

  std::string str;
  str.resize(length);

  va_start(args, fmt);
  vsnprintf(&str[0], length + 1, fmt, args);
  va_end(args);

  this->print(str);
}

#ifdef USE_WEBSERVER
AsyncEventSource::~AsyncEventSource() {
  LockGuard guard{this->pending_mutex_};
  for (auto *vec : {&this->sessions_, &this->pending_sessions_}) {
    for (auto *ses : *vec) {
      delete ses;  // NOLINT(cppcoreguidelines-owning-memory)
    }
  }
}

void AsyncEventSource::handleRequest(AsyncWebServerRequest *request) {
  // Httpd task: set up the live httpd_req_t and park the session; main loop does the rest.
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory,clang-analyzer-cplusplus.NewDeleteLeaks)
  auto *rsp = new AsyncEventSourceResponse(request, this, this->web_server_);
  {
    LockGuard guard{this->pending_mutex_};
    this->pending_sessions_.push_back(rsp);
    this->has_pending_sessions_.store(true, std::memory_order_release);
  }
  this->web_server_->enable_loop_soon_any_context();
}

// clang-analyzer traces a false-positive leak path from loop() through
// adopt_pending_sessions_main_loop_() into start_session_main_loop_() and
// finally ArduinoJson. Suppress along the entire in-our-code call chain.
// NOLINTBEGIN(clang-analyzer-cplusplus.NewDeleteLeaks)
bool AsyncEventSource::loop() {
  // Fast path: one atomic load per tick. Slow path is out-of-line on connect.
  if (this->has_pending_sessions_.load(std::memory_order_acquire)) {
    this->adopt_pending_sessions_main_loop_();
  }

  // Clean up dead sessions safely
  // This follows the ESP-IDF pattern where free_ctx marks resources as dead
  // and the main loop handles the actual cleanup to avoid race conditions
  for (size_t i = 0; i < this->sessions_.size();) {
    auto *ses = this->sessions_[i];
    // If the session has a dead socket (marked by destroy callback)
    if (ses->fd_.load() == 0) {
      // destroy() already logged the close with the fd; don't double-log here.
      delete ses;  // NOLINT(cppcoreguidelines-owning-memory)
      // Remove by swapping with last element (O(1) removal, order doesn't matter for sessions)
      this->sessions_[i] = this->sessions_.back();
      this->sessions_.pop_back();
    } else {
      ses->loop();
      ++i;
    }
  }
  return !this->sessions_.empty();
}

void AsyncEventSource::adopt_pending_sessions_main_loop_() {
  std::vector<AsyncEventSourceResponse *> incoming;
  {
    LockGuard guard{this->pending_mutex_};
    incoming.swap(this->pending_sessions_);
    this->has_pending_sessions_.store(false, std::memory_order_relaxed);
  }
  for (auto *rsp : incoming) {
    // Already disconnected? Drop it; skip on_connect_/session start on a dead session.
    if (rsp->fd_.load() == 0) {
      delete rsp;  // NOLINT(cppcoreguidelines-owning-memory)
      continue;
    }
    this->sessions_.push_back(rsp);
    // Prime first so on_connect_ observes a session that has already sent its
    // initial ping/config/sorting_groups, matching the pre-refactor ordering.
    rsp->start_session_main_loop_();
    if (this->on_connect_) {
      this->on_connect_(rsp);
    }
  }
}
// NOLINTEND(clang-analyzer-cplusplus.NewDeleteLeaks)

void AsyncEventSource::try_send_nodefer(const char *message, size_t message_len, const char *event, uint32_t id,
                                        uint32_t reconnect) {
  for (auto *ses : this->sessions_) {
    if (ses->fd_.load() != 0) {  // Skip dead sessions
      ses->try_send_nodefer(message, message_len, event, id, reconnect);
    }
  }
}

void AsyncEventSource::deferrable_send_state(void *source, const char *event_type,
                                             message_generator_t *message_generator) {
  // Skip if no connected clients to avoid unnecessary processing
  if (this->empty())
    return;
  for (auto *ses : this->sessions_) {
    if (ses->fd_.load() != 0) {  // Skip dead sessions
      ses->deferrable_send_state(source, event_type, message_generator);
    }
  }
}

AsyncEventSourceResponse::AsyncEventSourceResponse(const AsyncWebServerRequest *request,
                                                   esphome::web_server_idf::AsyncEventSource *server,
                                                   esphome::web_server::WebServer *ws)
    : server_(server), web_server_(ws), entities_iterator_(ws, server) {
  // Httpd task only. start_session_main_loop_() handles event_buffer_ / iterator setup.
  httpd_req_t *req = *request;

  httpd_resp_set_status(req, HTTPD_200);
  httpd_resp_set_type(req, "text/event-stream");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(req, "Connection", "keep-alive");

  for (const auto &header : DefaultHeaders::Instance().headers_) {
    httpd_resp_set_hdr(req, header.name, header.value);
  }

  httpd_resp_send_chunk(req, CRLF_STR, CRLF_LEN);

  req->sess_ctx = this;
  req->free_ctx = AsyncEventSourceResponse::destroy;

  this->hd_ = req->handle;
  this->fd_.store(httpd_req_to_sockfd(req));

  // Use non-blocking send to prevent watchdog timeouts when TCP buffers are full
  httpd_sess_set_send_override(this->hd_, this->fd_.load(), nonblocking_send);
}

// NOLINTBEGIN(clang-analyzer-cplusplus.NewDeleteLeaks) false positive with ArduinoJson
void AsyncEventSourceResponse::start_session_main_loop_() {
  auto *ws = this->web_server_;

  if (!this->event_buffer_) {
    this->event_buffer_ = RAMAllocator<char>().make_unique_array_for_overwrite(EVENT_BUFFER_CAPACITY);
    if (!this->event_buffer_) {
      ESP_LOGE(TAG, "Unable to allocate %zu-byte EventSource buffer; closing session", EVENT_BUFFER_CAPACITY);
      this->fd_.store(0);
      return;
    }
  }
  this->event_buffer_size_ = 0;
  this->event_bytes_sent_ = 0;

  // tcp send buffer is empty on connect, so these should always go through
  auto message = ws->get_config_json();
  this->try_send_nodefer(message.c_str(), message.size(), "ping", millis(), 30000);

#ifdef USE_WEBSERVER_SORTING
  for (auto &group : ws->sorting_groups_) {
    json::JsonBuilder builder;
    JsonObject root = builder.root();
    root["name"] = group.second.name;
    root["sorting_weight"] = group.second.weight;
    message = builder.serialize();

    // a (very) large number of these should be able to be queued initially without defer
    // since the only thing in the send buffer at this point is the initial ping/config
    this->try_send_nodefer(message.c_str(), message.size(), "sorting_group");
  }
#endif

  this->entities_iterator_.begin(ws->include_internal_);
}
// NOLINTEND(clang-analyzer-cplusplus.NewDeleteLeaks)

void AsyncEventSourceResponse::destroy(void *ptr) {
  auto *rsp = static_cast<AsyncEventSourceResponse *>(ptr);
  int fd = rsp->fd_.exchange(0);  // Atomically get and clear fd
  ESP_LOGD(TAG, "Event source connection closed (fd: %d)", fd);
  // Mark as dead - will be cleaned up in the main loop
  // Note: We don't delete or remove from set here to avoid race conditions
  // httpd will call our custom close_fn (safe_close_with_shutdown) which handles
  // shutdown() before close() to prevent lwIP race conditions
}

// helper for allowing only unique entries in the queue
void AsyncEventSourceResponse::deq_push_back_with_dedup_(void *source, message_generator_t *message_generator) {
  DeferredEvent item(source, message_generator);

  // Use range-based for loop instead of std::find_if to reduce template instantiation overhead and binary size
  for (auto &event : this->deferred_queue_) {
    if (event == item) {
      return;  // Already in queue, no need to update since items are equal
    }
  }
  this->deferred_queue_.push_back(item);
}

json::SerializationBuffer<> AsyncEventSourceResponse::generate_state_message_(
    void *source, message_generator_t *message_generator) {
  json::JsonArena<json::JSON_ARENA_SIZE> arena;
  json::JsonBuilder builder(&arena);
  message_generator(this->web_server_, source, builder);
  return builder.serialize();
}

void AsyncEventSourceResponse::process_deferred_queue_() {
  while (!deferred_queue_.empty()) {
    DeferredEvent &de = deferred_queue_.front();
    auto message = this->generate_state_message_(de.source_, de.message_generator_);
    if (this->try_send_nodefer(message.c_str(), message.size(), "state")) {
      // O(n) but memory efficiency is more important than speed here which is why std::vector was chosen
      deferred_queue_.erase(deferred_queue_.begin());
    } else {
      break;
    }
  }
}

bool AsyncEventSourceResponse::append_event_buffer_(const char *data, size_t len) {
  if (len == 0)
    return true;
  if (data == nullptr || !this->event_buffer_ || len > EVENT_BUFFER_CAPACITY - this->event_buffer_size_)
    return false;
  std::memcpy(this->event_buffer_.get() + this->event_buffer_size_, data, len);
  this->event_buffer_size_ += len;
  return true;
}

void AsyncEventSourceResponse::process_buffer_() {
  if (this->event_buffer_size_ == 0) {
    return;
  }
  if (this->event_bytes_sent_ == this->event_buffer_size_) {
    this->event_buffer_size_ = 0;
    this->event_bytes_sent_ = 0;
    return;
  }

  size_t remaining = this->event_buffer_size_ - this->event_bytes_sent_;
  int bytes_sent = httpd_socket_send(this->hd_, this->fd_.load(),
                                     this->event_buffer_.get() + this->event_bytes_sent_, remaining, 0);
  if (bytes_sent == HTTPD_SOCK_ERR_TIMEOUT) {
    this->consecutive_send_failures_++;
    if (this->consecutive_send_failures_ >= MAX_CONSECUTIVE_SEND_FAILURES) {
      ESP_LOGW(TAG, "Closing stuck EventSource connection after %" PRIu16 " failed sends",
               this->consecutive_send_failures_);
      this->fd_.store(0);
      this->deferred_queue_.clear();
      this->event_buffer_size_ = 0;
      this->event_bytes_sent_ = 0;
    }
    return;
  }
  if (bytes_sent == HTTPD_SOCK_ERR_FAIL) {
    return;
  }
  if (bytes_sent <= 0) {
    ESP_LOGW(TAG, "Unexpected send result: %d", bytes_sent);
    return;
  }

  this->consecutive_send_failures_ = 0;
  this->event_bytes_sent_ += bytes_sent;
  if (this->event_bytes_sent_ < this->event_buffer_size_) {
    ESP_LOGV(TAG, "Partial send: %d/%zu bytes (total: %zu/%zu)", bytes_sent, remaining,
             this->event_bytes_sent_, this->event_buffer_size_);
  }
  if (this->event_bytes_sent_ == this->event_buffer_size_) {
    this->event_buffer_size_ = 0;
    this->event_bytes_sent_ = 0;
  }
}

void AsyncEventSourceResponse::loop() {
  process_buffer_();
  process_deferred_queue_();
  this->entities_iterator_.try_advance(1);
}

bool AsyncEventSourceResponse::try_send_nodefer(const char *message, size_t message_len, const char *event, uint32_t id,
                                                uint32_t reconnect) {
  if (this->fd_.load() == 0 || !this->event_buffer_) {
    return false;
  }

  process_buffer_();
  if (this->event_buffer_size_ != 0) {
    return false;
  }

  auto append = [this](const char *data, size_t len) {
    if (this->append_event_buffer_(data, len))
      return true;
    ESP_LOGW(TAG, "Dropping oversized EventSource frame (capacity=%zu bytes)", EVENT_BUFFER_CAPACITY);
    this->event_buffer_size_ = 0;
    this->event_bytes_sent_ = 0;
    return false;
  };

  const char chunk_len_header[] = "        " CRLF_STR;
  const size_t chunk_len_header_len = sizeof(chunk_len_header) - 1;
  if (!append(chunk_len_header, chunk_len_header_len))
    return true;

  constexpr size_t num_buf_size = 32;
  char num_buf[num_buf_size];
  if (reconnect) {
    int len = snprintf(num_buf, num_buf_size, "retry: %" PRIu32 CRLF_STR, reconnect);
    if (len <= 0 || !append(num_buf, static_cast<size_t>(len)))
      return true;
  }
  if (id) {
    int len = snprintf(num_buf, num_buf_size, "id: %" PRIu32 CRLF_STR, id);
    if (len <= 0 || !append(num_buf, static_cast<size_t>(len)))
      return true;
  }
  if (event && *event) {
    if (!append("event: ", sizeof("event: ") - 1) || !append(event, std::strlen(event)) ||
        !append(CRLF_STR, CRLF_LEN))
      return true;
  }

  if (message) {
    const char *first_n = static_cast<const char *>(memchr(message, '\n', message_len));
    const char *first_r = static_cast<const char *>(memchr(message, '\r', message_len));
    if (first_n == nullptr && first_r == nullptr) {
      if (!append("data: ", sizeof("data: ") - 1) || !append(message, message_len) ||
          !append(CRLF_STR CRLF_STR, CRLF_LEN * 2))
        return true;
    } else {
      const char *line_start = message;
      const char *msg_end = message + message_len;
      const char *next_n = first_n;
      const char *next_r = first_r;
      while (line_start <= msg_end) {
        const char *line_end;
        const char *next_line;
        if (next_n == nullptr && next_r == nullptr) {
          if (!append("data: ", sizeof("data: ") - 1) ||
              !append(line_start, static_cast<size_t>(msg_end - line_start)) || !append(CRLF_STR, CRLF_LEN))
            return true;
          break;
        }
        if (next_n != nullptr && next_r != nullptr) {
          if (next_r + 1 == next_n) {
            line_end = next_r;
            next_line = next_n + 1;
          } else {
            line_end = (next_r < next_n) ? next_r : next_n;
            next_line = line_end + 1;
          }
        } else if (next_n != nullptr) {
          line_end = next_n;
          next_line = next_n + 1;
        } else {
          line_end = next_r;
          next_line = next_r + 1;
        }
        if (!append("data: ", sizeof("data: ") - 1) ||
            !append(line_start, static_cast<size_t>(line_end - line_start)) || !append(CRLF_STR, CRLF_LEN))
          return true;
        line_start = next_line;
        if (line_start >= msg_end)
          break;
        next_n = static_cast<const char *>(memchr(line_start, '\n', msg_end - line_start));
        next_r = static_cast<const char *>(memchr(line_start, '\r', msg_end - line_start));
      }
      if (!append(CRLF_STR, CRLF_LEN))
        return true;
    }
  }

  if (this->event_buffer_size_ == chunk_len_header_len) {
    this->event_buffer_size_ = 0;
    return true;
  }
  if (!append(CRLF_STR, CRLF_LEN))
    return true;

  const size_t chunk_len = this->event_buffer_size_ - CRLF_LEN - chunk_len_header_len;
  char chunk_len_str[9];
  snprintf(chunk_len_str, sizeof(chunk_len_str), "%08x", static_cast<unsigned>(chunk_len));
  std::memcpy(this->event_buffer_.get(), chunk_len_str, 8);

  this->event_bytes_sent_ = 0;
  process_buffer_();
  return true;
}

void AsyncEventSourceResponse::deferrable_send_state(void *source, const char *event_type,
                                                     message_generator_t *message_generator) {
  // allow all json "details_all" to go through before publishing bare state events, this avoids unnamed entries showing
  // up in the web GUI and reduces event load during initial connect
  if (!this->entities_iterator_.completed() && 0 != strcmp(event_type, "state_detail_all"))
    return;

  if (source == nullptr)
    return;
  if (event_type == nullptr)
    return;
  if (message_generator == nullptr)
    return;

  if (0 != strcmp(event_type, "state_detail_all") && 0 != strcmp(event_type, "state")) {
    ESP_LOGE(TAG, "Can't defer non-state event");
  }

  process_buffer_();
  process_deferred_queue_();

  if (this->event_buffer_size_ != 0 || !deferred_queue_.empty()) {
    // outgoing event buffer or deferred queue still not empty which means downstream tcp send buffer full, no point
    // trying to send first
    deq_push_back_with_dedup_(source, message_generator);
  } else {
    auto message = this->generate_state_message_(source, message_generator);
    if (!this->try_send_nodefer(message.c_str(), message.size(), "state")) {
      deq_push_back_with_dedup_(source, message_generator);
    }
  }
}
#endif

#ifdef USE_WEBSERVER_OTA
esp_err_t AsyncWebServer::handle_multipart_upload_(httpd_req_t *r, const char *content_type) {
  // Parse boundary and create reader
  const char *boundary_start;
  size_t boundary_len;
  if (!parse_multipart_boundary(content_type, &boundary_start, &boundary_len)) {
    ESP_LOGE(TAG, "Failed to parse multipart boundary");
    httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, nullptr);
    return ESP_FAIL;
  }

  AsyncWebServerRequest req(r);
  AsyncWebHandler *handler = nullptr;
  for (auto *h : this->handlers_) {
    if (h->canHandle(&req)) {
      handler = h;
      break;
    }
  }

  if (!handler) {
    ESP_LOGW(TAG, "No handler found for OTA request");
    httpd_resp_send_err(r, HTTPD_404_NOT_FOUND, nullptr);
    return ESP_OK;
  }

  // Upload state
  std::string filename;
  size_t index = 0;
  // Create reader on heap to reduce stack usage
  auto reader = std::make_unique<MultipartReader>("--" + std::string(boundary_start, boundary_len));

  // Configure callbacks
  reader->set_data_callback([&](const uint8_t *data, size_t len) {
    if (!reader->has_file() || !len)
      return;

    if (filename.empty()) {
      filename = reader->get_current_part().filename;
      ESP_LOGV(TAG, "Processing file: '%s'", filename.c_str());
      handler->handleUpload(&req, filename, 0, nullptr, 0, false);  // Start
    }

    handler->handleUpload(&req, filename, index, const_cast<uint8_t *>(data), len, false);
    index += len;
  });

  reader->set_part_complete_callback([&]() {
    if (index > 0) {
      handler->handleUpload(&req, filename, index, nullptr, 0, true);  // End
      filename.clear();
      index = 0;
    }
  });

  auto buffer = std::make_unique_for_overwrite<char[]>(RECV_CHUNK_SIZE);
  size_t bytes_since_yield = 0;

  for (size_t remaining = r->content_len; remaining > 0;) {
    int recv_len = httpd_req_recv(r, buffer.get(), std::min(remaining, RECV_CHUNK_SIZE));

    if (recv_len <= 0) {
#ifdef USE_WEBSERVER_AUTH
      if (!req.auth_rejected())
#endif
      httpd_resp_send_err(r, recv_len == HTTPD_SOCK_ERR_TIMEOUT ? HTTPD_408_REQ_TIMEOUT : HTTPD_400_BAD_REQUEST,
                          nullptr);
      return recv_len == HTTPD_SOCK_ERR_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_FAIL;
    }

    if (reader->parse(buffer.get(), recv_len) != static_cast<size_t>(recv_len)) {
      ESP_LOGW(TAG, "Multipart parser error");
#ifdef USE_WEBSERVER_AUTH
      if (!req.auth_rejected())
#endif
      httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, nullptr);
      return ESP_FAIL;
    }

    remaining -= recv_len;
    bytes_since_yield += recv_len;

    if (bytes_since_yield > YIELD_INTERVAL_BYTES) {
      vTaskDelay(1);
      bytes_since_yield = 0;
    }
  }

  handler->handleRequest(&req);
  return ESP_OK;
}
#endif  // USE_WEBSERVER_OTA

}  // namespace esphome::web_server_idf

#endif  // !defined(USE_ESP32)
