#pragma once
using esp_err_t = int;
using esp_http_client_handle_t = void *;
constexpr int ESP_OK = 0, ESP_FAIL = -1, HTTP_EVENT_ON_DATA = 1, HTTP_EVENT_ON_HEADER = 2, HTTP_EVENT_ON_CONNECTED = 3, HTTP_METHOD_GET = 0;
struct esp_http_client_event_t { int event_id; int data_len; void *user_data; void *data; const char *header_key; const char *header_value; };
struct esp_http_client_config_t {
  const char *url;
  int method, timeout_ms;
  const char *user_agent;
  esp_err_t (*event_handler)(esp_http_client_event_t *);
  void *user_data;
  int buffer_size, buffer_size_tx;
};
inline void *esp_http_client_init(const esp_http_client_config_t *) { return nullptr; }
inline void esp_http_client_set_header(void *, const char *, const char *) {}
inline int esp_http_client_perform(void *) { return ESP_FAIL; }
inline int esp_http_client_get_status_code(void *) { return 0; }
inline void esp_http_client_cleanup(void *) {}
inline const char *esp_err_to_name(int) { return "HOST_NO_NETWORK"; }
