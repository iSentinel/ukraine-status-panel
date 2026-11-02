#include "spotify_auth.h"
#include "stage4_transport.h"
#include "stage4_playback_facts.h"
#include "assets.h"
#include "token_transport.h"
#include "playback_transport.h"
#include "artwork_transport.h"
#include "artwork_decoder.h"
#include "control_playback.h"
#include "control_transport.h"
#include "esp_heap_caps.h"
#include "freertos/idf_additions.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/log.h"
#include <cstring>
#include <string>
#include <string_view>

namespace esphome::usp_spotify_auth {
namespace {
constexpr char TAG[] = "usp.spotify.auth";
using Request = web_server_idf::AsyncWebServerRequest;
using usp_spotify_auth_form::Form;
using usp_spotify_auth_form::MAX_BODY;
using usp_spotify_auth_return::Status;
}

/** Registered metadata is bounded, HTTPS-only and safe for status JSON. */
bool SpotifyAuthApi::application_configured_() const {
  const std::string_view client(this->application_client_id_.data());
  const std::string_view redirect(this->application_redirect_uri_.data());
  if (!usp_spotify_pkce::valid_client_id(client) ||
      !usp_spotify_auth_return::valid_redirect(redirect)) return false;
  for (const char ch : redirect)
    if (ch == '"' || ch == '\\') return false;
  return true;
}

bool SpotifyAuthApi::application_matches_(const Form &form) const {
  return this->application_configured_() &&
      form.client_id == std::string_view(this->application_client_id_.data()) &&
      form.redirect_uri == std::string_view(this->application_redirect_uri_.data());
}

/** Register only behind the existing private Digest/Origin middleware. */
void SpotifyAuthApi::setup() {
#ifdef USE_WEBSERVER_AUTH
  if (!this->application_configured_()) {
    this->mark_failed();
    return;
  }
  if (web_server_base::global_web_server_base == nullptr) {
    this->mark_failed();
    return;
  }
  ExternalRAMAllocator<char> allocator(ExternalRAMAllocator<char>::ALLOC_EXTERNAL);
  this->body_ = allocator.make_unique_array_for_overwrite(MAX_BODY + 1);
  if (!this->body_) { this->mark_failed(); return; }
  this->clear_body_();
  web_server_base::global_web_server_base->add_handler(this);
  this->registered_.store(true, std::memory_order_release);
  this->session_persistence_ready_=this->setup_persistence_();
  if (!this->start_worker_()) ESP_LOGW(TAG, "Token worker unavailable; sign-in disabled");
#else
  this->mark_failed();
#endif
}

/** Flash calls require an internal task stack; large secret buffers stay in PSRAM. */
bool SpotifyAuthApi::setup_persistence_() {
  ExternalRAMAllocator<usp_spotify_session::Workspace> allocator(
      ExternalRAMAllocator<usp_spotify_session::Workspace>::ALLOC_EXTERNAL);
  this->session_workspace_=allocator.make_unique();
  if (!this->session_workspace_) return false;
  this->session_vault_.set_workspace(this->session_workspace_.get());
  if (!this->session_key_configured_ || !this->session_storage_.setup() ||
      !this->session_crypto_.setup(this->session_storage_,this->session_key_id_)) return false;
  this->session_vault_.set_key_id(this->session_crypto_.active_key_id());
  return this->session_persistence_.configure(this->application_client_id_.data());
}

/** Never wait for an HTTP callback from the rendering/main task. */
void SpotifyAuthApi::loop() {
  if (this->language_) {
    const uint8_t language = this->language_();
    this->cached_language_.store(language < 3 ? language : 0);
  }
  if (this->artwork_presenter_) this->artwork_presenter_();
  std::unique_lock<std::mutex> lock(this->state_mutex_, std::try_to_lock);
  if (!lock.owns_lock()) return;
  if (this->exchange_available_.load()) this->exchange_.expire(millis());
  else this->exchange_.cancel();
}

void SpotifyAuthApi::dump_config() {
  ESP_LOGCONFIG(TAG, "Local authorization registered=%s; application=%s; token worker=%s",
                YESNO(this->registered_.load(std::memory_order_acquire)),
                YESNO(this->application_configured_()),
                YESNO(this->worker_running_.load(std::memory_order_acquire)));
}

/** Route names are fixed; values are accepted only from the bounded POST body. */
Route SpotifyAuthApi::route_(Request *request) const {
  char buffer[Request::URL_BUF_SIZE];
  const auto url = request->url_to(buffer);
  if (url == "/usp/spotify/auth/") return Route::PAGE;
  if (url == "/usp/spotify/auth/page.js") return Route::PAGE_JS;
  if (url == "/usp/spotify/auth/page.css") return Route::PAGE_CSS;
  if (url == "/usp/spotify/auth/return.js") return Route::RETURN_JS;
  if (url == "/usp/spotify/auth/status") return Route::STATUS;
  if (url == "/usp/spotify/auth/begin") return Route::BEGIN;
  if (url == "/usp/spotify/auth/callback") return Route::CALLBACK;
  if (url == "/usp/spotify/auth/cancel") return Route::CANCEL;
  return Route::NONE;
}

bool SpotifyAuthApi::canHandle(Request *request) const {
  return this->registered_.load(std::memory_order_acquire) && this->route_(request) != Route::NONE;
}

/** text/plain keeps the framework on its chunked custom-handler path. */
bool SpotifyAuthApi::plain_body_(Request *request) const {
  const auto header = request->get_header("Content-Type");
  if (!header.has_value()) return request->contentLength() == 0;
  std::string_view value(header.value());
  value = value.substr(0, value.find(';'));
  if (value.size() != 10) return false;
  constexpr char expected[] = "text/plain";
  for (size_t i = 0; i < value.size(); ++i) {
    char ch = value[i];
    if (ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a');
    if (ch != expected[i]) return false;
  }
  return true;
}

/** Admit fragments only in order, without heap growth or arithmetic overflow. */
void SpotifyAuthApi::handleBody(
    Request *request, uint8_t *data, size_t length, size_t index, size_t total) {
  if (index == 0) {
    this->clear_body_();
    this->body_request_ = request;
  }
  if (!this->body_ || this->body_request_ != request || this->body_rejected_) return;
  if (!this->plain_body_(request) || total > MAX_BODY || index != this->received_ ||
      index > total || length > total - index || (length && data == nullptr)) {
    this->body_rejected_ = true;
    return;
  }
  if (length) std::memcpy(this->body_.get() + index, data, length);
  this->received_ += length;
  this->body_[this->received_] = 0;
}

bool SpotifyAuthApi::complete_body_(Request *request) const {
  return this->body_ && !this->body_rejected_ &&
      request->contentLength() <= MAX_BODY &&
      (request->contentLength() == 0 ||
       (this->body_request_ == request && this->received_ == request->contentLength()));
}

void SpotifyAuthApi::clear_body_() {
  if (this->body_) usp_spotify_pkce::zeroize(this->body_.get(), MAX_BODY + 1);
  this->body_request_ = nullptr;
  this->received_ = 0;
  this->body_rejected_ = false;
}

/** Do not cache responses, authorization URLs or callback values. */
void SpotifyAuthApi::respond_(Request *request, int code, const char *body) const {
  httpd_resp_set_hdr(*request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(*request, "Referrer-Policy", "no-referrer");
  request->send(code, "application/json", body);
}

/** Reject query values, unsupported methods, incomplete bodies and field aliases. */
void SpotifyAuthApi::handleRequest(Request *request) {
  const auto route = this->route_(request);
  if (!this->registered_.load(std::memory_order_acquire) || route == Route::NONE) return;
  if (std::strchr(static_cast<httpd_req_t *>(*request)->uri, '?') != nullptr) {
    this->respond_(request, 400, "{\"ok\":false,\"error\":\"query_not_allowed\"}");
  } else if (route >= Route::PAGE && route <= Route::RETURN_JS) {
    if (request->method() == HTTP_GET) this->asset_(request, route);
    else this->respond_(request, 405, "{\"ok\":false,\"error\":\"method_not_allowed\"}");
  } else if (route == Route::STATUS && request->method() == HTTP_GET) {
    this->status_(request);
  } else if (route == Route::STATUS || request->method() != HTTP_POST) {
    this->respond_(request, 405, "{\"ok\":false,\"error\":\"method_not_allowed\"}");
  } else if (!this->plain_body_(request) || !this->complete_body_(request)) {
    this->respond_(request, 400, "{\"ok\":false,\"error\":\"invalid_body\"}");
  } else {
    Form form;
    if (!form.parse(this->body_.get(), request->contentLength(), MAX_BODY + 1))
      this->respond_(request, 400, "{\"ok\":false,\"error\":\"invalid_form\"}");
    else
      this->dispatch_(request, route, form);
  }
  this->clear_body_();
}

void SpotifyAuthApi::dispatch_(Request *request, Route route, const Form &form) {
  std::unique_lock<std::mutex> lock(this->state_mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    this->respond_(request, 503, "{\"ok\":false,\"error\":\"busy\"}");
    return;
  }
  if (route != Route::CANCEL && !this->exchange_available_.load()) {
    this->exchange_.cancel();
    this->respond_(request, 503, "{\"ok\":false,\"error\":\"exchange_unavailable\"}");
    return;
  }
  if (route == Route::BEGIN && form.begin_shape()) this->begin_(request, form);
  else if (route == Route::CALLBACK && form.callback_shape()) this->callback_(request, form);
  else if (route == Route::CANCEL && form.fields == 0) {
    if (!this->clear_session_and_advance_()) {
      this->respond_(request,503,"{\"ok\":false,\"error\":\"session_clear_busy\"}");return;
    }
    this->exchange_.cancel();
    this->playback_state_ = {};
    this->clear_stage4_state_();
    this->artwork_cache_.select(this->generation(),{});
    this->accepted_at_ = 0;
    this->worker_phase_ = usp_spotify_tokens::Phase::IDLE;
    this->respond_(request, 200, "{\"ok\":true,\"state\":\"idle\"}");
  } else this->respond_(request, 400, "{\"ok\":false,\"error\":\"invalid_fields\"}");
}

/** No new RNG work or replacement while a session or one-time code is active. */
void SpotifyAuthApi::begin_(Request *request, const Form &form) {
  if (!this->application_matches_(form)) {
    this->respond_(request, 400, "{\"ok\":false,\"error\":\"invalid_configuration\"}");
    return;
  }
  const uint32_t now = millis();
  if (this->exchange_.status(now) != Status::IDLE ||
      this->worker_phase_ == usp_spotify_tokens::Phase::EXCHANGING ||
      this->worker_phase_ == usp_spotify_tokens::Phase::CONNECTED) {
    this->respond_(request, 409, "{\"ok\":false,\"error\":\"authorization_active\"}");
    return;
  }
  if (!this->clear_session_and_advance_()) {
    this->respond_(request,503,"{\"ok\":false,\"error\":\"session_clear_busy\"}");return;
  }
  usp_spotify_pkce::Session prepared;
  const bool generated = usp_spotify_pkce::begin(&prepared, now);
  const bool adopted = generated && this->exchange_.adopt(
      &prepared, now, form.client_id, form.redirect_uri);
  prepared.clear();
  if (!adopted) {
    this->respond_(request, 503, "{\"ok\":false,\"error\":\"session_unavailable\"}");
    return;
  }
  this->playback_state_ = {};
  this->clear_stage4_state_();
  this->artwork_cache_.select(this->generation(),{});
  this->worker_phase_ = usp_spotify_tokens::Phase::IDLE;
  const auto url = this->exchange_.authorization_url(now);
  const std::string body = "{\"ok\":true,\"authorize_url\":\"" + url + "\"}";
  this->respond_(request, 200, body.c_str());
}

void SpotifyAuthApi::callback_(Request *request, const Form &form) {
  const uint32_t now = millis();
  const auto result = this->exchange_.accept(now, form.state, form.code, form.error);
  if (result == usp_spotify_pkce::CallbackResult::ACCEPT_CODE) {
    this->accepted_at_ = now;
    this->respond_(request, 202, "{\"ok\":true,\"state\":\"code_pending\"}");
  } else if (result == usp_spotify_pkce::CallbackResult::ACCEPT_ERROR) {
    this->worker_phase_ = usp_spotify_tokens::Phase::IDLE;
    this->respond_(request, 200, "{\"ok\":true,\"state\":\"denied\"}");
  } else this->respond_(request, 400, "{\"ok\":false,\"error\":\"callback_rejected\"}");
}

const char *SpotifyAuthApi::session_storage_state_() {
  if (!this->session_key_configured_ || !this->session_persistence_ready_ || this->session_storage_.io_error())
    return "error";
  switch (this->session_crypto_.mode()) {
    case usp_spotify_session::ProtectionMode::HARDWARE: return "hardware";
    case usp_spotify_session::ProtectionMode::LOCAL: return "local";
    case usp_spotify_session::ProtectionMode::NONE: return "error";
  }
  return "error";
}

void SpotifyAuthApi::status_(Request *request) {
  std::unique_lock<std::mutex> lock(this->state_mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    this->respond_(request, 503, "{\"ok\":false,\"error\":\"busy\"}");
    return;
  }
  const bool available = this->exchange_available_.load();
  if (!available) {
    this->exchange_.cancel();
    this->worker_phase_ = usp_spotify_tokens::Phase::IDLE;
  }
  const auto status = this->exchange_.status(millis());
  const char *phase = this->worker_phase_ == usp_spotify_tokens::Phase::CONNECTED ? "connected" :
      this->worker_phase_ == usp_spotify_tokens::Phase::EXCHANGING ? "exchanging" :
      this->worker_phase_ == usp_spotify_tokens::Phase::REAUTH ? "reauth" : "idle";
  const char *state = status == Status::AWAITING_USER ? "awaiting_user" :
      status == Status::CODE_PENDING ? "code_pending" : phase;
  const char *languages[] = {"uk", "ru", "en"};
  const auto language = this->cached_language_.load();
  const std::string body = std::string("{\"ok\":true,\"state\":\"") + state +
      "\",\"exchange_available\":" + (available ? "true" : "false") +
      ",\"language\":\"" + languages[language < 3 ? language : 0] +
      "\",\"session_storage\":\"" + this->session_storage_state_() +
      "\",\"session_key_id\":" + std::to_string(this->session_key_id_) +
      ",\"application\":{\"client_id\":\"" + this->application_client_id_.data() +
      "\",\"redirect_uri\":\"" + this->application_redirect_uri_.data() + "\"}}";
  this->respond_(request, 200, body.c_str());
}

/** The existing authenticated handler serves gzip directly from immutable flash. */
void SpotifyAuthApi::asset_(Request *request, Route route) {
  const uint8_t *data = nullptr;
  size_t size = 0;
  const char *type = "text/javascript;charset=UTF-8";
  switch (route) {
    case Route::PAGE:
      data = assets::INDEX; size = sizeof(assets::INDEX); type = "text/html;charset=UTF-8"; break;
    case Route::PAGE_JS:
      data = assets::PAGE_JS; size = sizeof(assets::PAGE_JS); break;
    case Route::PAGE_CSS:
      data = assets::PAGE_CSS; size = sizeof(assets::PAGE_CSS); type = "text/css;charset=UTF-8"; break;
    case Route::RETURN_JS:
      data = assets::RETURN_JS; size = sizeof(assets::RETURN_JS); break;
    default: return;
  }
  httpd_resp_set_hdr(*request, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(*request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(*request, "Referrer-Policy", "no-referrer");
  httpd_resp_set_hdr(*request, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(*request, "Content-Security-Policy",
      "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; "
      "img-src 'none'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'");
  auto *response = request->beginResponse(200, type, data, size);
  request->send(response);
}

/** Take exactly once; the original callback deadline survives worker admission. */
bool SpotifyAuthApi::take(usp_spotify_auth_return::TokenRequest *output,
                          uint32_t *generation, uint32_t *accepted_at) {
  if (!output || !generation || !accepted_at) return false;
  output->clear();
  std::unique_lock<std::mutex> lock(this->state_mutex_, std::try_to_lock);
  if (!lock.owns_lock() || !this->exchange_available_.load() || this->stop_worker_.load() ||
      !this->exchange_.take(millis(), output)) return false;
  *generation = this->generation_.load(std::memory_order_acquire);
  *accepted_at = this->accepted_at_;
  this->worker_phase_ = usp_spotify_tokens::Phase::EXCHANGING;
  return true;
}

/** A stale or cancelled worker cannot restore a connected state. */
bool SpotifyAuthApi::publish(uint32_t generation, usp_spotify_tokens::Phase phase) {
  std::unique_lock<std::mutex> lock(this->state_mutex_, std::try_to_lock);
  if (!lock.owns_lock() || !this->exchange_available_.load() || this->stop_worker_.load() ||
      generation != this->generation_.load(std::memory_order_acquire)) return false;
  this->worker_phase_ = phase;
  if (phase != usp_spotify_tokens::Phase::CONNECTED) {
    this->clear_stage4_state_();
    this->artwork_cache_.select(generation,{});
  }
  return true;
}

/** Persisted logout/new-auth transition is atomic with respect to worker vault I/O. */
bool SpotifyAuthApi::clear_session_and_advance_() {
  if (!this->session_persistence_ready_) return false;
  return this->session_persistence_.clear_then_advance(nullptr);
}

/** Each button submits one action; an unavailable or busy permission check fails closed. */
bool SpotifyAuthApi::request_control(usp_spotify_controls::Command command) {
  if (this->stop_worker_.load(std::memory_order_acquire)) return false;
  if (this->stage4_coordinator_)
    return this->stage4_coordinator_->request_basic(this->generation(),command,millis()) != 0;
  return this->control_coordinator_.request(this->generation(),command,millis()) != 0;
}

/** Select Play/Pause from the same fresh permission facts; main never waits. */
bool SpotifyAuthApi::request_play_pause() {
  if (this->stop_worker_.load(std::memory_order_acquire)) return false;
  if (this->stage4_coordinator_)
    return this->stage4_coordinator_->request_play_pause(this->generation(),millis()) != 0;
  return this->control_coordinator_.request_play_pause(this->generation(),millis()) != 0;
}

bool SpotifyAuthApi::copy_controls(usp_spotify_controls::ControlView *output) {
  return this->control_coordinator_.copy_view(this->generation(), millis(), output);
}

/** Copy only presentation state; access/refresh tokens never leave the worker context. */
bool SpotifyAuthApi::publish(
    uint32_t generation, const usp_spotify_playback_worker::PlaybackState &state) {
  std::unique_lock<std::mutex> lock(this->state_mutex_, std::try_to_lock);
  if (!lock.owns_lock() || !this->exchange_available_.load() || this->stop_worker_.load() ||
      generation != this->generation_.load(std::memory_order_acquire)) return false;
  this->playback_state_ = state;
  const auto &snapshot = state.snapshot;
  const bool permitted = state.kind == usp_spotify_playback::ResponseKind::JSON &&
      snapshot.valid && snapshot.has_item && !snapshot.private_session;
  const size_t url_size = strnlen(snapshot.image_url.data(),snapshot.image_url.size());
  this->artwork_cache_.select(generation,
      permitted ? std::string_view(snapshot.image_url.data(),url_size) : std::string_view{});
  lock.unlock();
  this->control_coordinator_.facts(generation, usp_spotify_controls::playback_facts(state));
  if (this->stage4_coordinator_) {
    this->stage4_coordinator_->transfer_action(
        generation,snapshot.transfer.known,snapshot.transfer.allowed);
    this->stage4_coordinator_->value_facts(
        generation,usp_spotify_stage4_binding::value_facts(snapshot,state.updated_at));
    this->stage4_coordinator_->mode_facts(
        generation,usp_spotify_stage4_binding::mode_facts(snapshot,state.updated_at));
    this->stage4_coordinator_->queue_facts(
        generation,usp_spotify_stage4_binding::queue_facts(snapshot,state.updated_at));
  }
  return true;
}

bool SpotifyAuthApi::copy_playback(
    usp_spotify_playback_worker::PlaybackState *output,
    SpotifyAuthApi::PlaybackPhase *phase) {
  if (!output) return false;
  std::unique_lock<std::mutex> lock(this->state_mutex_, std::try_to_lock);
  if (!lock.owns_lock()) return false;
  *output = this->playback_state_;
  if (phase != nullptr) *phase = this->worker_phase_;
  return true;
}

/** Shutdown only signals cancellation; it never waits in the rendering loop. */
void SpotifyAuthApi::on_shutdown() {
  this->exchange_available_.store(false);
  this->stop_worker_.store(true, std::memory_order_release);
  this->artwork_cache_.stop();
  this->devices_refresh_requested_.store(false);
  this->queue_refresh_requested_.store(false);
  this->generation_.fetch_add(1, std::memory_order_acq_rel);
}

/** Components have application lifetime; destruction cooperatively joins the worker. */
SpotifyAuthApi::~SpotifyAuthApi() {
  this->on_shutdown();
  while (this->worker_running_.load(std::memory_order_acquire)) delay(1);
  this->clear_body_();
}

/** Caller already holds state_mutex_ when presentation must be invalidated. */
void SpotifyAuthApi::clear_stage4_state_() {
  if(this->stage4_state_)this->stage4_state_->clear();
  this->devices_refresh_requested_.store(false,std::memory_order_release);
  this->queue_refresh_requested_.store(false,std::memory_order_release);
}

bool SpotifyAuthApi::request_devices_refresh() {
  if(this->stop_worker_.load(std::memory_order_acquire)||!this->stage4_coordinator_)return false;
  std::unique_lock<std::mutex> lock(this->state_mutex_,std::try_to_lock);
  if(!lock.owns_lock()||this->worker_phase_!=usp_spotify_tokens::Phase::CONNECTED)return false;
  lock.unlock();
  if(!this->stage4_coordinator_->api_ready(millis()))return false;
  bool expected=false;
  return this->devices_refresh_requested_.compare_exchange_strong(expected,true,std::memory_order_acq_rel);
}

bool SpotifyAuthApi::request_queue_refresh() {
  if(this->stop_worker_.load(std::memory_order_acquire)||!this->stage4_coordinator_)return false;
  std::unique_lock<std::mutex> lock(this->state_mutex_,std::try_to_lock);
  if(!lock.owns_lock()||this->worker_phase_!=usp_spotify_tokens::Phase::CONNECTED)return false;
  lock.unlock();
  if(!this->stage4_coordinator_->api_ready(millis()))return false;
  bool expected=false;
  return this->queue_refresh_requested_.compare_exchange_strong(expected,true,std::memory_order_acq_rel);
}

bool SpotifyAuthApi::request_transfer(uint8_t index,uint32_t revision) {
  return !this->stop_worker_.load(std::memory_order_acquire)&&this->stage4_coordinator_&&
      this->stage4_coordinator_->request_transfer(this->generation(),index,revision,millis())!=0;
}
bool SpotifyAuthApi::request_volume(uint32_t value) {
  return !this->stop_worker_.load(std::memory_order_acquire)&&this->stage4_coordinator_&&
      this->stage4_coordinator_->request_volume(this->generation(),value,millis())!=0;
}
bool SpotifyAuthApi::request_seek(uint32_t value) {
  return !this->stop_worker_.load(std::memory_order_acquire)&&this->stage4_coordinator_&&
      this->stage4_coordinator_->request_seek(this->generation(),value,millis())!=0;
}
bool SpotifyAuthApi::request_shuffle_toggle() {
  return !this->stop_worker_.load(std::memory_order_acquire)&&this->stage4_coordinator_&&
      this->stage4_coordinator_->request_shuffle(this->generation(),millis())!=0;
}
bool SpotifyAuthApi::request_repeat(usp_spotify_modes::Repeat mode) {
  return !this->stop_worker_.load(std::memory_order_acquire)&&this->stage4_coordinator_&&
      this->stage4_coordinator_->request_repeat(this->generation(),mode,millis())!=0;
}
bool SpotifyAuthApi::request_queue_add(std::string_view uri) {
  return !this->stop_worker_.load(std::memory_order_acquire)&&this->stage4_coordinator_&&
      this->stage4_coordinator_->request_queue_add(this->generation(),uri,millis())!=0;
}

bool SpotifyAuthApi::copy_devices(usp_spotify_devices::Catalog *output,uint32_t *revision,uint32_t *updated_at) {
  if(!output)return false;
  std::unique_lock<std::mutex> lock(this->state_mutex_,std::try_to_lock);
  if(!lock.owns_lock())return false;
  output->clear();if(revision)*revision=0;if(updated_at)*updated_at=0;
  if(!this->stage4_state_||this->stage4_state_->generation!=this->generation())return true;
  *output=this->stage4_state_->devices;
  if(revision)*revision=this->stage4_state_->devices_revision;
  if(updated_at)*updated_at=this->stage4_state_->devices_updated_at;
  return true;
}

bool SpotifyAuthApi::copy_queue(usp_spotify_queue::Queue *output,uint32_t *updated_at) {
  if(!output)return false;
  std::unique_lock<std::mutex> lock(this->state_mutex_,std::try_to_lock);
  if(!lock.owns_lock())return false;
  output->clear();if(updated_at)*updated_at=0;
  if(!this->stage4_state_||this->stage4_state_->generation!=this->generation())return true;
  *output=this->stage4_state_->queue;
  if(updated_at)*updated_at=this->stage4_state_->queue_updated_at;
  return true;
}

/** Large advanced-read scratch stays in strict PSRAM, outside the task stack. */
bool SpotifyAuthApi::setup_stage4_() {
  ExternalRAMAllocator<usp_spotify_stage4_binding::Coordinator> stage4_allocator(
      ExternalRAMAllocator<usp_spotify_stage4_binding::Coordinator>::ALLOC_EXTERNAL);
  this->stage4_coordinator_=stage4_allocator.make_unique(this->control_coordinator_);
  ExternalRAMAllocator<usp_spotify_stage4_binding::SharedState> stage4_state_allocator(
      ExternalRAMAllocator<usp_spotify_stage4_binding::SharedState>::ALLOC_EXTERNAL);
  this->stage4_state_=stage4_state_allocator.make_unique();
  ExternalRAMAllocator<char> stage4_body_allocator(ExternalRAMAllocator<char>::ALLOC_EXTERNAL);
  this->stage4_body_=stage4_body_allocator.make_unique_array_for_overwrite(STAGE4_QUEUE_BODY);
  ExternalRAMAllocator<usp_spotify_stage4_binding::ReadWorkspace> scratch_allocator(
      ExternalRAMAllocator<usp_spotify_stage4_binding::ReadWorkspace>::ALLOC_EXTERNAL);
  this->stage4_workspace_=scratch_allocator.make_unique();
  if (!this->stage4_coordinator_ || !this->stage4_state_ || !this->stage4_body_ || !this->stage4_workspace_) {
    this->stage4_body_.reset();this->stage4_state_.reset();this->stage4_coordinator_.reset();
    this->stage4_workspace_.reset();return false;
  }
  return true;
}

/** Allocation and task creation must both succeed before sign-in becomes available. */
bool SpotifyAuthApi::start_worker_() {
  if (!IdfTokenTransport::available()) return false;
  ExternalRAMAllocator<usp_spotify_tokens::Engine> allocator(
      ExternalRAMAllocator<usp_spotify_tokens::Engine>::ALLOC_EXTERNAL);
  this->worker_context_ = allocator.make_unique();
  if (!this->worker_context_) return false;
  ExternalRAMAllocator<usp_spotify_playback_worker::Worker> playback_allocator(
      ExternalRAMAllocator<usp_spotify_playback_worker::Worker>::ALLOC_EXTERNAL);
  this->playback_context_ = playback_allocator.make_unique();
  if (!this->playback_context_) {
    this->worker_context_.reset();
    return false;
  }
  if(!this->setup_stage4_()) {
    this->playback_context_.reset();this->worker_context_.reset();return false;
  }
  this->stop_worker_.store(false);
  this->worker_running_.store(true, std::memory_order_release);
  const auto result = xTaskCreatePinnedToCoreWithCaps(
      worker_entry_, "usp_spotify", 12288, this, 1, &this->worker_task_, 0,
      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (result != pdPASS) {
    this->worker_running_.store(false, std::memory_order_release);
    this->stage4_body_.reset();
    this->stage4_workspace_.reset();
    this->stage4_state_.reset();
    this->stage4_coordinator_.reset();
    this->playback_context_.reset();
    this->worker_context_.reset();
    return false;
  }
  this->exchange_available_.store(true);
  return true;
}

namespace {
class WorkerClock final : public usp_spotify_tokens::Clock,
                          public usp_spotify_playback_worker::WorkerClock {
 public:
  uint32_t now() const override { return millis(); }
};

class PlaybackJsonAllocator final : public ArduinoJson::Allocator {
 public:
  PlaybackJsonAllocator()
      : allocator_(ExternalRAMAllocator<uint8_t>::ALLOC_EXTERNAL) {}
  void *allocate(size_t size) override { return allocator_.allocate(size); }
  void deallocate(void *ptr) override {
    allocator_.deallocate(static_cast<uint8_t *>(ptr), 1);
  }
  void *reallocate(void *ptr, size_t size) override {
    return allocator_.reallocate(static_cast<uint8_t *>(ptr), size);
  }
 private:
  ExternalRAMAllocator<uint8_t> allocator_;
};

class PlaybackParser final : public usp_spotify_playback_worker::WorkerParser {
 public:
  bool parse(const char *body, size_t size,
             usp_spotify_playback::PlaybackSnapshot *output) override {
    if (!body || !size || !output) return false;
    PlaybackJsonAllocator allocator;
    JsonDocument document(&allocator);
    const auto error = deserializeJson(document, body, size);
    if (error != DeserializationError::Ok || document.overflowed() ||
        !document.is<JsonObject>()) return false;
    return usp_spotify_playback::parse_playback(
        document.as<JsonObjectConst>(), output);
  }
};
/** The artwork decoder receives only bounded bytes and a cancellation callback. */
class ArtworkDecoder {
 public:
  usp_spotify_artwork_decode::Result decode(const uint8_t *input,size_t size,
      void *workspace,uint16_t *pixels,size_t count,
      usp_spotify_artwork_decode::Image &image,bool (*current)(void *),void *owner) {
    return usp_spotify_artwork_decode::decode(
        input,size,workspace,pixels,count,image,current,owner);
  }
};

bool parse_devices_body(const char *body,size_t size,usp_spotify_devices::Catalog *output) {
  if(!body||!size||!output)return false;
  PlaybackJsonAllocator allocator;JsonDocument document(&allocator);
  const auto error=deserializeJson(document,body,size);
  return error==DeserializationError::Ok&&!document.overflowed()&&document.is<JsonObject>()&&
      usp_spotify_devices::parse_into(document.as<JsonObjectConst>(),output);
}

bool parse_queue_body(const char *body,size_t size,usp_spotify_queue::Queue *output) {
  if(!body||!size||!output)return false;
  PlaybackJsonAllocator allocator;JsonDocument document(&allocator);
  const auto error=deserializeJson(document,body,size);
  return error==DeserializationError::Ok&&!document.overflowed()&&document.is<JsonObject>()&&
      usp_spotify_queue::parse_into(document.as<JsonObjectConst>(),output);
}

bool has_queue_read_scope(std::string_view scope) {
  return usp_spotify_controls::has_scope(scope,"user-read-playback-state") ||
      usp_spotify_controls::has_scope(scope,"user-read-currently-playing");
}

}

/** Scope is read only by the token-owning worker, never by the rendering task. */
void SpotifyAuthApi::sync_controls_() {
  const auto generation=this->generation();
  const auto now=millis();
  this->control_coordinator_.session(
      generation,this->worker_context_->current(generation) &&
      this->worker_context_->has_modify_scope(now));
  if (this->stage4_coordinator_) {
    const auto scope=this->worker_context_->current(generation)?this->worker_context_->scope(now):std::string_view{};
    this->stage4_coordinator_->session(generation,scope);
  }
}

/** Recheck the borrowed command immediately before an actual HTTPS attempt. */
bool SpotifyAuthApi::control_current_(
    void *owner, const usp_spotify_controls::Request &request) {
  auto *self = static_cast<SpotifyAuthApi *>(owner);
  return !self->stop_worker_.load(std::memory_order_acquire) &&
      self->control_coordinator_.current(self->generation(), request, millis());
}

/** A single attempt is followed by observation, never by resending the command. */
void SpotifyAuthApi::dispatch_control_() {
  usp_spotify_controls::Request request{};
  if (!this->control_coordinator_.take(this->generation(), millis(), &request)) return;
  IdfControlTransport transport;
  const auto response = transport.perform(
      request, this->worker_context_->access(millis()), control_current_, this);
  this->control_coordinator_.finish(this->generation(), request, response, millis());
  if (this->control_coordinator_.take_reauth()) this->worker_context_->force_reauth(
      this->session_persistence_ready_?&this->session_persistence_:nullptr);
}

bool SpotifyAuthApi::transfer_current_(void *owner,const usp_spotify_devices::TransferRequest &request) {
  auto *self=static_cast<SpotifyAuthApi *>(owner);
  return !self->stop_worker_.load(std::memory_order_acquire)&&self->stage4_coordinator_&&
      self->stage4_coordinator_->current(self->generation(),request,millis());
}
bool SpotifyAuthApi::value_current_(void *owner,const usp_spotify_value_controls::Request &request) {
  auto *self=static_cast<SpotifyAuthApi *>(owner);
  return !self->stop_worker_.load(std::memory_order_acquire)&&self->stage4_coordinator_&&
      self->stage4_coordinator_->current(self->generation(),request,millis());
}
bool SpotifyAuthApi::mode_current_(void *owner,const usp_spotify_modes::Request &request) {
  auto *self=static_cast<SpotifyAuthApi *>(owner);
  return !self->stop_worker_.load(std::memory_order_acquire)&&self->stage4_coordinator_&&
      self->stage4_coordinator_->current(self->generation(),request,millis());
}
bool SpotifyAuthApi::queue_current_(void *owner,const usp_spotify_queue::AddRequest &request) {
  auto *self=static_cast<SpotifyAuthApi *>(owner);
  return !self->stop_worker_.load(std::memory_order_acquire)&&self->stage4_coordinator_&&
      self->stage4_coordinator_->current(self->generation(),request,millis());
}

/** At most one advanced mutation is taken and attempted once per worker iteration. */
void SpotifyAuthApi::dispatch_stage4_() {
  if(!this->stage4_coordinator_||!this->worker_context_)return;
  const uint32_t generation=this->generation(),now=millis();
  if(!this->worker_context_->current(generation)||!this->stage4_coordinator_->api_ready(now))return;
  const auto token=this->worker_context_->access(now);if(token.empty())return;
  IdfStage4Transport transport;Stage4HttpResult response{};bool taken=false;
  switch(this->stage4_coordinator_->family()) {
    case usp_spotify_stage4_binding::Family::DEVICE: {
      usp_spotify_devices::TransferRequest request{};
      if(!this->stage4_coordinator_->take_transfer(generation,now,&request))break;
      taken=true;response=transport.transfer(request,token,transfer_current_,this);
      this->stage4_coordinator_->finish(generation,request,response.status,response.complete,
          response.attempted,response.retry_known,response.retry_excessive,response.retry_seconds,millis());break;
    }
    case usp_spotify_stage4_binding::Family::VALUE: {
      usp_spotify_value_controls::Request request{};
      if(!this->stage4_coordinator_->take_value(generation,now,&request))break;
      taken=true;response=transport.value(request,token,value_current_,this);
      this->stage4_coordinator_->finish(generation,request,response.status,response.complete,
          response.attempted,response.retry_known,response.retry_excessive,response.retry_seconds,millis());break;
    }
    case usp_spotify_stage4_binding::Family::MODE: {
      usp_spotify_modes::Request request{};
      if(!this->stage4_coordinator_->take_mode(generation,now,&request))break;
      taken=true;response=transport.mode(request,token,mode_current_,this);
      this->stage4_coordinator_->finish(generation,request,response.status,response.complete,
          response.attempted,response.retry_known,response.retry_excessive,response.retry_seconds,millis());break;
    }
    case usp_spotify_stage4_binding::Family::QUEUE: {
      usp_spotify_queue::AddRequest request{};
      if(!this->stage4_coordinator_->take_queue(generation,now,&request))break;
      taken=true;response=transport.queue_add(request,token,queue_current_,this);
      this->stage4_coordinator_->finish(generation,request,response.status,response.complete,
          response.attempted,response.retry_known,response.retry_excessive,response.retry_seconds,millis());break;
    }
    case usp_spotify_stage4_binding::Family::NONE:break;
  }
  if(taken&&response.attempted&&this->generation()==generation&&this->playback_context_)
    this->playback_context_->request_poll();
  if(taken&&response.attempted&&response.status==401&&this->generation()==generation&&
      this->worker_context_->current(generation))
    this->worker_context_->force_reauth(this->session_persistence_ready_?&this->session_persistence_:nullptr);
}

/** Fetch into worker-owned PSRAM; shared state changes only after complete parsing. */
void SpotifyAuthApi::poll_devices_(uint32_t generation,std::string_view token,std::string_view scope) {
  auto &catalog=this->stage4_workspace_->devices;catalog.clear();
  IdfStage4Transport transport;Stage4HttpResult response{};
  if(this->stage4_coordinator_->api_ready(millis())&&
     usp_spotify_controls::has_scope(scope,"user-read-playback-state"))
    response=transport.devices(token,this->stage4_body_.get(),STAGE4_DEVICE_BODY);
  this->stage4_coordinator_->provider_response(response.status,response.attempted,
      response.retry_known,response.retry_excessive,response.retry_seconds,millis());
  const bool parsed=response.attempted&&response.complete&&response.status==200&&
      parse_devices_body(this->stage4_body_.get(),response.size,&catalog);
  usp_spotify_pkce::zeroize(this->stage4_body_.get(),STAGE4_QUEUE_BODY);
  if(!parsed)this->stage4_coordinator_->invalidate_devices(generation);
  {
    std::unique_lock<std::mutex> lock(this->state_mutex_,std::try_to_lock);
    if(lock.owns_lock()&&generation==this->generation()) {
      if(parsed) {
        this->stage4_coordinator_->device_catalog(generation,catalog,millis());
        this->stage4_state_->devices=catalog;
        this->stage4_state_->devices_revision=this->stage4_coordinator_->device_revision();
        this->stage4_state_->devices_updated_at=millis();
      } else {
        this->stage4_state_->devices.clear();this->stage4_state_->devices_revision=0;
        this->stage4_state_->devices_updated_at=0;
      }
      this->stage4_state_->generation=generation;
    }
  }
  catalog.clear();
  if(response.attempted&&response.status==401&&this->generation()==generation&&
     this->worker_context_->current(generation))
    this->worker_context_->force_reauth(this->session_persistence_ready_?&this->session_persistence_:nullptr);
}

/** A busy state mutex skips publication; an unowned lock is never unlocked. */
void SpotifyAuthApi::poll_queue_(uint32_t generation,std::string_view token,std::string_view scope) {
  auto &queue=this->stage4_workspace_->queue;queue.clear();
  IdfStage4Transport transport;Stage4HttpResult response{};
  if(this->stage4_coordinator_->api_ready(millis())&&has_queue_read_scope(scope))
    response=transport.queue(token,this->stage4_body_.get(),STAGE4_QUEUE_BODY);
  this->stage4_coordinator_->provider_response(response.status,response.attempted,
      response.retry_known,response.retry_excessive,response.retry_seconds,millis());
  const bool parsed=response.attempted&&response.complete&&response.status==200&&
      parse_queue_body(this->stage4_body_.get(),response.size,&queue);
  usp_spotify_pkce::zeroize(this->stage4_body_.get(),STAGE4_QUEUE_BODY);
  {
    std::unique_lock<std::mutex> lock(this->state_mutex_,std::try_to_lock);
    if(lock.owns_lock()&&generation==this->generation()) {
      if(parsed)this->stage4_state_->queue=queue;
      else this->stage4_state_->queue.clear();
      this->stage4_state_->queue_updated_at=parsed?millis():0;
      this->stage4_state_->generation=generation;
    }
  }
  queue.clear();
  if(response.attempted&&response.status==401&&this->generation()==generation&&
     this->worker_context_->current(generation))
    this->worker_context_->force_reauth(this->session_persistence_ready_?&this->session_persistence_:nullptr);
}

/** Devices and queue are fetched only after an explicit UI refresh request. */
void SpotifyAuthApi::poll_stage4_reads_() {
  if(!this->stage4_coordinator_||!this->stage4_state_||!this->stage4_body_||
     !this->stage4_workspace_||!this->worker_context_)return;
  const uint32_t generation=this->generation();
  if(!this->worker_context_->current(generation))return;
  if(this->devices_refresh_requested_.exchange(false,std::memory_order_acq_rel)) {
    const auto token=this->worker_context_->access(millis());
    if(!token.empty())this->poll_devices_(generation,token,this->worker_context_->scope(millis()));
  }
  // A devices 401 may clear the token; never reuse its old string view.
  if(!this->worker_context_->current(generation))return;
  if(this->queue_refresh_requested_.exchange(false,std::memory_order_acq_rel)) {
    const auto token=this->worker_context_->access(millis());
    if(!token.empty())this->poll_queue_(generation,token,this->worker_context_->scope(millis()));
  }
}

/** Token and Player HTTPS share one internal-stack worker with PSRAM data; token bytes never reach UI/main. */
void SpotifyAuthApi::worker_entry_(void *argument) {
  auto *self = static_cast<SpotifyAuthApi *>(argument);
  IdfTokenTransport token_transport;
  IdfPlaybackTransport playback_transport;
  PlaybackParser playback_parser;
  IdfArtworkTransport artwork_transport;
  ArtworkDecoder artwork_decoder;
  WorkerClock clock;
  usp_spotify_controls::ObservedPlaybackTransport observed_playback(
      playback_transport, self->control_coordinator_, clock);
  while (!self->stop_worker_.load(std::memory_order_acquire)) {
    self->worker_context_->step(*self, token_transport, clock,
        self->session_persistence_ready_?&self->session_persistence_:nullptr);
    self->sync_controls_();
    self->dispatch_control_();
    self->sync_controls_();
    self->dispatch_stage4_();
    self->sync_controls_();
    self->poll_stage4_reads_();
    const auto access = self->worker_context_->access(clock.now());
    usp_spotify_controls::OwnedPlaybackBridge playback_owner(
        *self, self->worker_context_->generation());
    usp_spotify_controls::poll_playback(self->control_coordinator_,
        *self->playback_context_, playback_owner, access, observed_playback, playback_parser, clock);
    if (self->playback_context_->take_reauth() &&
        self->worker_context_->current(self->generation())) {
      self->worker_context_->force_reauth(
          self->session_persistence_ready_?&self->session_persistence_:nullptr);
      self->sync_controls_();
    }
    self->artwork_cache_.step(artwork_transport,artwork_decoder,clock);
    vTaskDelay(pdMS_TO_TICKS(250));
  }
  self->playback_context_->clear();
  self->worker_context_->clear();
  self->worker_running_.store(false, std::memory_order_release);
  vTaskDeleteWithCaps(nullptr);
}
}  // namespace esphome::usp_spotify_auth
