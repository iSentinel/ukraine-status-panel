#include "artwork_transport.h"
#include "esphome/components/network/util.h"
#include "esphome/components/usp_weather/network_gate.h"
#include "esphome/core/hal.h"
#include "esp_http_client.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

namespace esphome::usp_spotify_auth {
namespace {
struct Receiver {
  usp_spotify_artwork_http::Collector body;
  bool (*current)(void *);
  void *owner;
  bool cancelled{false};
  uint32_t started{millis()};
};
/** Consume every response header, including Retry-After after an invalid MIME. */
esp_err_t receive(esp_http_client_event_t *event) {
  if (!event || !event->user_data) return ESP_FAIL;
  auto &state = *static_cast<Receiver *>(event->user_data);
  if (uint32_t(millis() - state.started) >= usp_spotify_artwork_http::DEADLINE)
    return ESP_FAIL;
  if (state.current && !state.current(state.owner)) {
    state.cancelled = true;
    return ESP_FAIL;
  }
  if (event->event_id == HTTP_EVENT_ON_HEADER) {
    if (!event->header_key || !event->header_value) return ESP_FAIL;
    const size_t key_size = strnlen(event->header_key, 64);
    const size_t value_size = strnlen(event->header_value, 129);
    state.body.header(std::string_view(event->header_key, key_size),
                      std::string_view(event->header_value, value_size));
    return ESP_OK;
  }
  if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
  if (event->data_len < 0) return ESP_FAIL;
  return state.body.append(event->data, size_t(event->data_len), millis())
      ? ESP_OK : ESP_FAIL;
}
esp_http_client_config_t configuration(const char *url, Receiver *receiver) {
  esp_http_client_config_t config{};
  config.url = url;
  config.method = HTTP_METHOD_GET;
  config.transport_type = HTTP_TRANSPORT_OVER_SSL;
  config.timeout_ms = 5000;
  config.disable_auto_redirect = true;
  config.max_authorization_retries = -1;
  config.keep_alive_enable = false;
  config.skip_cert_common_name_check = false;
  config.buffer_size = 2048;
  config.buffer_size_tx = 512;
  config.event_handler = receive;
  config.user_data = receiver;
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
  config.crt_bundle_attach = esp_crt_bundle_attach;
#endif
  return config;
}
bool valid_request(const usp_spotify_artwork::Request &request) {
  const size_t length = strnlen(request.url.data(), request.url.size());
  return request.ticket && length <= usp_spotify_artwork::MAX_URL &&
      usp_spotify_artwork::allowed_url(std::string_view(request.url.data(), length));
}
ArtworkResult execute(esp_http_client_handle_t client, Receiver &receiver) {
  ArtworkResult result;
  const bool prepared =
      esp_http_client_set_header(client, "Accept", "image/jpeg") == ESP_OK &&
      esp_http_client_set_header(client, "Accept-Encoding", "identity") == ESP_OK;
  if (prepared) {
    result.attempted = true;
    const auto error = esp_http_client_perform(client);
    result.status = esp_http_client_get_status_code(client);
    result.complete = error == ESP_OK && receiver.body.complete(
        result.status, esp_http_client_is_complete_data_received(client), millis());
  }
  result.size = receiver.body.size();
  result.retry = receiver.body.cooldown;
  result.cancelled = receiver.cancelled ||
      (receiver.current && !receiver.current(receiver.owner));
  if (result.cancelled) result.complete = false;
  return result;
}
}  // namespace

bool IdfArtworkTransport::available() {
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
  return true;
#else
  return false;
#endif
}
ArtworkResult IdfArtworkTransport::perform(
    const usp_spotify_artwork::Request &request, uint8_t *output, size_t capacity,
    bool (*current)(void *), void *owner) {
  if (!output || capacity < 4 || capacity > usp_spotify_artwork_http::MAX_BODY ||
      !valid_request(request) || !available() || !network::is_connected() ||
      !rate_.allows(millis()) || (current && !current(owner))) return {};
  usp_network::HttpsTransaction admission(
      "spotify-artwork", usp_network::HttpsPriority::BACKGROUND, 250);
  if (!admission.acquired() || (current && !current(owner))) return {};
  Receiver receiver{{output, capacity, millis()}, current, owner, false};
  auto config = configuration(request.url.data(), &receiver);
  const auto client = esp_http_client_init(&config);
  if (!client) return {};
  const auto result = execute(client, receiver);
  esp_http_client_cleanup(client);
  rate_.observe(result, millis());
  return result;
}
}  // namespace esphome::usp_spotify_auth
