#pragma once
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/web_server_idf/web_server_idf.h"
#include "usp_spotify_auth_form.h"
#include "usp_spotify_auth_return.h"
#include <mutex>
#include <atomic>
#include <functional>
#include <string>
#include "token_worker_engine.h"
#include "playback_model.h"
#include "playback_worker_engine.h"
#include "playback_presentation.h"
#include "artwork_cache.h"
#include "control_coordinator.h"
#include "stage4_coordinator.h"
#include "stage4_state.h"
#include "session_vault_idf.h"
#include "session_coordinator.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace esphome::usp_spotify_auth {
enum class Route : uint8_t { NONE, PAGE, PAGE_JS, PAGE_CSS, RETURN_JS, STATUS, BEGIN, CALLBACK, CANCEL };

/** Private authenticated HTTP adapter with a dedicated internal-stack token worker with PSRAM data. */
class SpotifyAuthApi : public Component, public web_server_idf::AsyncWebHandler,
                       public usp_spotify_tokens::Bridge,
                       public usp_spotify_playback_worker::WorkerBridge {
 public:
  ~SpotifyAuthApi() override;
  void on_shutdown() override;
  /** Build-time registered application metadata; Client Secret is never used. */
  void set_application_client_id(const std::string &value) {
    usp_spotify_auth_return::copy_field(this->application_client_id_, value);
  }
  void set_application_redirect_uri(const std::string &value) {
    usp_spotify_auth_return::copy_field(this->application_redirect_uri_, value);
  }
  /** Prefer this eFuse block when ready; otherwise use local persistent key. */
  void set_session_key_id(uint8_t value) {
    this->session_key_id_ = value;
    this->session_key_configured_ = value < 6;
  }
  /** Worker availability is internal; it cannot be enabled through a user setting. */
  void set_language(std::function<uint8_t()> callback) { this->language_ = std::move(callback); }
  /** Called only from Component::loop after LCD setup, outside the state lock. */
  void set_artwork_presenter(std::function<void()> callback) { this->artwork_presenter_ = std::move(callback); }
  /** Main task applies source/cache changes before returning true. */
  template<class Consumer> bool present_artwork(Consumer consumer) {
    return this->artwork_cache_.present(consumer);
  }
  void setup() override;
  void loop() override;
  void dump_config() override;
  bool canHandle(web_server_idf::AsyncWebServerRequest *request) const override;
  void handleBody(web_server_idf::AsyncWebServerRequest *, uint8_t *, size_t, size_t, size_t) override;
  void handleRequest(web_server_idf::AsyncWebServerRequest *request) override;
  bool isRequestHandlerTrivial() const override { return false; }
  uint32_t generation() const override { return this->generation_.load(std::memory_order_acquire); }
  bool take(usp_spotify_auth_return::TokenRequest *, uint32_t *, uint32_t *) override;
  using PlaybackPhase = usp_spotify_tokens::Phase;
  bool publish(uint32_t, usp_spotify_tokens::Phase) override;
  bool publish(uint32_t, const usp_spotify_playback_worker::PlaybackState &) override;
  /** Main submits a single explicit tap; HTTPS stays in the existing worker. */
  bool request_control(usp_spotify_controls::Command command);
  /** Select Play/Pause from the latest owned playback state, without optimism. */
  bool request_play_pause();
  /** Copy bounded permissions and command status, without waiting for a worker. */
  bool copy_controls(usp_spotify_controls::ControlView *output);
  bool copy_playback(usp_spotify_playback_worker::PlaybackState *output,
                     PlaybackPhase *phase = nullptr);
  bool request_devices_refresh();
  bool request_queue_refresh();
  bool request_transfer(uint8_t index,uint32_t catalog_revision);
  bool request_volume(uint32_t percent);
  bool request_seek(uint32_t position_ms);
  bool request_shuffle_toggle();
  bool request_repeat(usp_spotify_modes::Repeat mode);
  bool request_queue_add(std::string_view uri);
  bool copy_devices(usp_spotify_devices::Catalog *output,uint32_t *catalog_revision=nullptr,
                    uint32_t *updated_at=nullptr);
  bool copy_queue(usp_spotify_queue::Queue *output,uint32_t *updated_at=nullptr);

 protected:
  bool start_worker_();
  bool setup_persistence_();
  bool setup_stage4_();
  void sync_controls_();
  void dispatch_control_();
  void dispatch_stage4_();
  void poll_stage4_reads_();
  void poll_devices_(uint32_t,std::string_view,std::string_view);
  void poll_queue_(uint32_t,std::string_view,std::string_view);
  void clear_stage4_state_();
  bool clear_session_and_advance_();
  static bool control_current_(void *, const usp_spotify_controls::Request &);
  static bool transfer_current_(void *, const usp_spotify_devices::TransferRequest &);
  static bool value_current_(void *, const usp_spotify_value_controls::Request &);
  static bool mode_current_(void *, const usp_spotify_modes::Request &);
  static bool queue_current_(void *, const usp_spotify_queue::AddRequest &);
  bool application_configured_() const;
  bool application_matches_(const usp_spotify_auth_form::Form &) const;
  static void worker_entry_(void *);
  Route route_(web_server_idf::AsyncWebServerRequest *request) const;
  bool plain_body_(web_server_idf::AsyncWebServerRequest *request) const;
  bool complete_body_(web_server_idf::AsyncWebServerRequest *request) const;
  void clear_body_();
  void respond_(web_server_idf::AsyncWebServerRequest *, int, const char *) const;
  void dispatch_(web_server_idf::AsyncWebServerRequest *, Route, const usp_spotify_auth_form::Form &);
  void begin_(web_server_idf::AsyncWebServerRequest *, const usp_spotify_auth_form::Form &);
  void callback_(web_server_idf::AsyncWebServerRequest *, const usp_spotify_auth_form::Form &);
  void status_(web_server_idf::AsyncWebServerRequest *);
  const char *session_storage_state_();
  void asset_(web_server_idf::AsyncWebServerRequest *, Route);
  usp_spotify_controls::Coordinator control_coordinator_;
  RAMUniquePtr<usp_spotify_stage4_binding::Coordinator> stage4_coordinator_;
  RAMUniquePtr<usp_spotify_stage4_binding::SharedState> stage4_state_;
  RAMUniquePtr<usp_spotify_stage4_binding::ReadWorkspace> stage4_workspace_;
  RAMUniquePtr<char[]> stage4_body_;
  std::atomic<bool> devices_refresh_requested_{false},queue_refresh_requested_{false};
  std::atomic<uint32_t> generation_{0};
  RAMUniquePtr<usp_spotify_session::Workspace> session_workspace_;
  usp_spotify_session::IdfStorage session_storage_;
  usp_spotify_session::IdfCrypto session_crypto_;
  usp_spotify_session::Vault session_vault_{session_storage_,session_crypto_};
  usp_spotify_session::Coordinator<usp_spotify_session::Vault> session_persistence_{session_vault_,generation_};
  std::mutex state_mutex_;
  std::array<char, usp_spotify_pkce::MAX_CLIENT_ID + 1> application_client_id_{};
  std::array<char, usp_spotify_pkce::MAX_REDIRECT_URI + 1> application_redirect_uri_{};
  usp_spotify_artwork_cache::Cache artwork_cache_;
  std::function<void()> artwork_presenter_;
  usp_spotify_auth_return::Exchange exchange_;
  // Body storage belongs exclusively to the serialized httpd task.
  RAMUniquePtr<char[]> body_;
  web_server_idf::AsyncWebServerRequest *body_request_{nullptr};
  size_t received_{0};
  bool body_rejected_{false};
  std::atomic<bool> registered_{false};
  std::atomic<bool> exchange_available_{false};
  std::atomic<uint8_t> cached_language_{0};
  // The callback reads the ordinary global only from the main loop.
  std::function<uint8_t()> language_;
  std::atomic<bool> stop_worker_{false}, worker_running_{false};
  bool session_key_configured_{false},session_persistence_ready_{false};
  uint8_t session_key_id_{0};
  uint32_t accepted_at_{0};
  usp_spotify_tokens::Phase worker_phase_{usp_spotify_tokens::Phase::IDLE};
  TaskHandle_t worker_task_{nullptr};
  RAMUniquePtr<usp_spotify_tokens::Engine> worker_context_;
  RAMUniquePtr<usp_spotify_playback_worker::Worker> playback_context_;
  usp_spotify_playback_worker::PlaybackState playback_state_{};
};
static_assert(sizeof(SpotifyAuthApi) <= 4096, "Auth adapter object must stay bounded");
}  // namespace esphome::usp_spotify_auth
