#pragma once
#include "artwork_request_plan.h"
#include "artwork_frames.h"
#include "artwork_buffers.h"
#include "artwork_decoder.h"
#include <mutex>
#include <atomic>
#include <string_view>

namespace usp_spotify_artwork_cache {
struct Picture {
  const uint16_t *pixels;
  uint64_t ticket;
  uint16_t width, height;
  uint8_t index;
};
struct Work { usp_spotify_artwork::Request request{}; uint8_t index{255}; };

/** Own stable pixels and serialize metadata, never HTTP or JPEG work.
 * Component state may lock this cache; cache callbacks must never lock component
 * state in return. Main uses try_lock and owns source/cache invalidation. The
 * owner stops the worker and detaches LVGL before destroying this object.
 */
class Cache {
 public:
  /** Shutdown signals cancellation without waiting for a rendering/state lock. */
  void stop() { stopped_.store(true,std::memory_order_release); }
  /** Component passes only a current-generation, permitted artwork URL. */
  void select(uint32_t session, std::string_view url) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!session_seen_ || session_ != session) {
      session_seen_ = true;session_ = session;plan_.clear();invalidate_();
    }
    if (plan_.select(url)) invalidate_();
  }
  /** A callback may wait for a short metadata/source change, never for HTTPS. */
  bool current(uint64_t ticket) {
    std::lock_guard<std::mutex> lock(mutex_);
    return !stopped_.load(std::memory_order_acquire) && ticket && current_ticket_ == ticket;
  }
  /** Main callback returns true only after source/cache changes have finished.
   * False retains every loan for a later retry, including allocation failure.
   * Do not retain the Picture pointer or enter component/cache methods here.
   */
  template<class Consumer> bool present(Consumer consumer) {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) return false;
    if (stopped_.load(std::memory_order_acquire)) invalidate_();
    frames_.present();
    if (frames_.visible_current()) {
      const auto index = displayed_();
      const auto slot = frames_.view(index);
      if (slot.ticket != visible_ticket_) {
        const Picture picture{buffers_.frame(index),slot.ticket,
                              slot.width,slot.height,index};
        if (!consumer(&picture)) return false;
        visible_ticket_ = slot.ticket;
      }
    } else {
      frames_.detach();
      if ((visible_ticket_ || has_retired_()) && !consumer(nullptr)) return false;
      visible_ticket_ = 0;
    }
    acknowledge_retired_();
    return true;
  }
  /** Single-worker attempt; never call step concurrently. No tokens reach adapters. */
  template<class Transport, class Decoder, class Clock>
  void step(Transport &transport, Decoder &decoder, const Clock &clock) {
    const auto work = begin_(clock.now());
    if (!work.request.ticket) return;
    Guard guard{this,work.request.ticket};
    const auto result = transport.perform(work.request,buffers_.compressed(),
        usp_spotify_artwork_buffers::COMPRESSED_BYTES,Guard::check,&guard);
    usp_spotify_artwork_decode::Image image{};
    auto decoded = usp_spotify_artwork_decode::Result::CANCELLED;
    if (result.complete && Guard::check(&guard))
      decoded = decoder.decode(buffers_.compressed(),result.size,buffers_.workspace(),
          buffers_.frame(work.index),usp_spotify_artwork_decode::MAX_PIXELS,
          image,Guard::check,&guard);
    finish_(work,result,decoded,image,clock.now());
  }
 private:
  struct Guard {
    Cache *owner;uint64_t ticket;
    static bool check(void *value) {
      auto &guard = *static_cast<Guard *>(value);
      return guard.owner->current(guard.ticket);
    }
  };
  /** Called under cache lock; writers and displayed loans remain allocated. */
  void invalidate_() { current_ticket_ = 0;frames_.select(0); }
  Work begin_(uint32_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_.load(std::memory_order_acquire)) return {};
    auto request = plan_.begin(now);
    if (!request.ticket) return {};
    current_ticket_ = request.ticket;frames_.select(request.ticket);
    if (!buffers_.ensure()) {
      plan_.finish(request.ticket,false,now,10000);return {};
    }
    const auto index = frames_.begin(request.ticket);
    if (index == usp_spotify_frames::NONE) {
      plan_.finish(request.ticket,false,now,1000);return {};
    }
    return {request,index};
  }
  template<class Result>
  void finish_(const Work &work, const Result &result,
               usp_spotify_artwork_decode::Result decoded,
               const usp_spotify_artwork_decode::Image &image,uint32_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool decoded_ok = decoded == usp_spotify_artwork_decode::Result::OK;
    const bool accepted = !stopped_.load(std::memory_order_acquire) && decoded_ok &&
        current_ticket_ == work.request.ticket && image.width && image.height &&
        image.width <= 300 && image.height <= 300;
    const bool permanent_http = result.attempted && result.status >= 400 &&
        result.status < 500 && result.status != 408 && result.status != 429;
    const bool permanent_decode = result.complete &&
        (decoded == usp_spotify_artwork_decode::Result::INVALID ||
         decoded == usp_spotify_artwork_decode::Result::UNSUPPORTED ||
         decoded == usp_spotify_artwork_decode::Result::LIMIT);
    const uint32_t retry = result.status == 429 ? result.retry.delay : 5000U;
    frames_.finish(work.index,work.request.ticket,
                   accepted ? image.width : 0,accepted ? image.height : 0);
    plan_.finish(work.request.ticket,accepted,now,retry,
                 !result.cancelled && (permanent_http || permanent_decode));
  }
  uint8_t displayed_() const {
    for (uint8_t i=0;i<2;++i)
      if (frames_.view(i).phase == usp_spotify_frames::Phase::DISPLAYED) return i;
    return usp_spotify_frames::NONE;
  }
  bool has_retired_() const {
    for (uint8_t i=0;i<2;++i)
      if (frames_.view(i).phase == usp_spotify_frames::Phase::RETIRED) return true;
    return false;
  }
  void acknowledge_retired_() {
    for (uint8_t i=0;i<2;++i) {
      const auto slot = frames_.view(i);
      if (slot.phase == usp_spotify_frames::Phase::RETIRED)
        frames_.acknowledge(i,slot.ticket);
    }
  }
  std::mutex mutex_;
  usp_spotify_artwork::RequestPlan plan_;
  usp_spotify_frames::Frames frames_;
  usp_spotify_artwork_buffers::Buffers buffers_;
  uint64_t current_ticket_{0},visible_ticket_{0};
  uint32_t session_{0};bool session_seen_{false};
  std::atomic<bool> stopped_{false};
};
static_assert(sizeof(Cache) <= 512, "Only bounded metadata belongs in Cache");
}  // namespace usp_spotify_artwork_cache
