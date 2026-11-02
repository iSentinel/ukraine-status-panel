#include "playback_transport.h"

#include "esphome/components/network/util.h"
#include "esphome/components/usp_weather/network_gate.h"
#include "esphome/core/hal.h"
#include "esp_http_client.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

#include <algorithm>
#include <array>
#include <cstring>

namespace esphome::usp_spotify_auth {
namespace {
using usp_spotify_tokens::HttpResult;
constexpr char ENDPOINT[] =
    "https://api.spotify.com/v1/me/player?additional_types=episode";
constexpr uint32_t DEADLINE_MS = 8000;

/** Accept delta-seconds Retry-After only; excessive values disable auto retry. */
void retry_header(const char *key, const char *value, HttpResult *result) {
  if (!key || !value || !result) return;
  size_t key_length = 0;
  while (key_length <= 11 && key[key_length]) ++key_length;
  if (key_length != 11) return;
  constexpr char expected[] = "retry-after";
  for (size_t i = 0; i < 11; ++i) {
    char ch = key[i];
    if (ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a');
    if (ch != expected[i]) return;
  }
  const size_t length = strnlen(value, 65);
  if (length == 65) return;
  size_t first = 0, last = length;
  while (first < last && (value[first] == ' ' || value[first] == '\t')) ++first;
  while (last > first && (value[last - 1] == ' ' || value[last - 1] == '\t')) --last;
  if (first == last) return;
  uint32_t seconds = 0;
  for (size_t i = first; i < last; ++i) {
    if (value[i] < '0' || value[i] > '9') return;
    seconds = std::min<uint32_t>(seconds * 10U + uint32_t(value[i] - '0'), 86401U);
  }
  if (seconds > 86400U) {
    result->retry_excess = true;
    return;
  }
  result->retry_valid = true;
  result->retry_ms = std::max(result->retry_ms,
                              std::max<uint32_t>(1000U, seconds * 1000U));
}

struct Collector {
  char *body;
  size_t capacity;
  size_t size{0};
  uint32_t started;
  bool overflow{false};
  HttpResult result{};
};

/** Copy only bounded response bytes; late or partial replies stay unconfirmed. */
esp_err_t collect(esp_http_client_event_t *event) {
  auto *collector = static_cast<Collector *>(event->user_data);
  if (!collector) return ESP_FAIL;
  if (event->event_id != HTTP_EVENT_ON_HEADER &&
      event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
  if (uint32_t(millis() - collector->started) >= DEADLINE_MS) {
    collector->overflow = true;
    return ESP_FAIL;
  }
  if (event->event_id == HTTP_EVENT_ON_HEADER) {
    retry_header(event->header_key, event->header_value, &collector->result);
    return ESP_OK;
  }
  if (event->data_len < 0 || (event->data_len && !event->data) ||
      collector->size > collector->capacity ||
      size_t(event->data_len) > collector->capacity - collector->size) {
    collector->overflow = true;
    return ESP_FAIL;
  }
  if (event->data_len) {
    std::memcpy(collector->body + collector->size, event->data,
                size_t(event->data_len));
  }
  collector->size += size_t(event->data_len);
  return ESP_OK;
}

esp_http_client_config_t configuration(Collector *collector) {
  esp_http_client_config_t config{};
  config.url = ENDPOINT;
  config.method = HTTP_METHOD_GET;
  config.transport_type = HTTP_TRANSPORT_OVER_SSL;
  config.timeout_ms = 5000;
  config.disable_auto_redirect = true;
  config.max_authorization_retries = -1;
  config.keep_alive_enable = false;
  config.skip_cert_common_name_check = false;
  config.buffer_size = 2048;
  config.buffer_size_tx = 1024;
  config.event_handler = collect;
  config.user_data = collector;
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
  config.crt_bundle_attach = esp_crt_bundle_attach;
#endif
  return config;
}

bool authorization_value(std::string_view token,
                         std::array<char, usp_spotify_tokens::MAX_TOKEN + 8> *output) {
  if (!output || !usp_spotify_tokens::bearer_value(token)) return false;
  output->fill(0);
  constexpr char prefix[] = "Bearer ";
  static_assert(sizeof(prefix) - 1 == 7);
  std::memcpy(output->data(), prefix, sizeof(prefix) - 1);
  std::memcpy(output->data() + sizeof(prefix) - 1, token.data(), token.size());
  (*output)[sizeof(prefix) - 1 + token.size()] = 0;
  return true;
}

bool prepare(esp_http_client_handle_t client, const char *authorization) {
  return esp_http_client_set_header(client, "Authorization", authorization) == ESP_OK &&
      esp_http_client_set_header(client, "Accept", "application/json") == ESP_OK &&
      esp_http_client_set_header(client, "Accept-Encoding", "identity") == ESP_OK;
}
}  // namespace

bool IdfPlaybackTransport::available() {
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
  return true;
#else
  return false;
#endif
}

HttpResult IdfPlaybackTransport::perform(
    std::string_view access_token, char *output, size_t capacity) {
  if (!output || !capacity || capacity > MAX_PLAYBACK_BODY || !this->available() ||
      !network::is_connected()) return {};
  std::array<char, usp_spotify_tokens::MAX_TOKEN + 8> authorization{};
  if (!authorization_value(access_token, &authorization)) return {};
  usp_network::HttpsTransaction admission(
      "spotify", usp_network::HttpsPriority::BACKGROUND, 250);
  if (!admission.acquired()) {
    usp_spotify_pkce::zeroize(authorization.data(), authorization.size());
    return {};
  }
  Collector collector{output, capacity, 0, millis(), false, {}};
  auto config = configuration(&collector);
  const auto client = esp_http_client_init(&config);
  if (!client) {
    usp_spotify_pkce::zeroize(authorization.data(), authorization.size());
    return {};
  }
  if (prepare(client, authorization.data())) {
    collector.result.attempted = true;
    const auto error = esp_http_client_perform(client);
    collector.result.status = esp_http_client_get_status_code(client);
    collector.result.complete = error == ESP_OK && !collector.overflow &&
        esp_http_client_is_complete_data_received(client);
  }
  collector.result.size = collector.size;
  esp_http_client_cleanup(client);
  usp_spotify_pkce::zeroize(authorization.data(), authorization.size());
  return collector.result;
}

}  // namespace esphome::usp_spotify_auth
