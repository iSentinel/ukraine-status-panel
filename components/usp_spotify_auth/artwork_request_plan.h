#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>

namespace usp_spotify_artwork {
constexpr size_t MAX_URL = 256;
/** Allow only the documented Spotify image origin and a simple image key.
 * Unknown origins keep the placeholder. This is not a general URL parser.
 * The future transport must also disable redirects and authorization headers.
 */
inline bool allowed_url(std::string_view url) {
  constexpr std::string_view prefix = "https://i.scdn.co/image/";
  if (url.size() <= prefix.size() || url.size() > MAX_URL ||
      url.substr(0, prefix.size()) != prefix) return false;
  for (char ch : url.substr(prefix.size())) {
    if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
          (ch >= '0' && ch <= '9'))) return false;
  }
  return true;
}

/** An owned request remains stable when the selected album changes. */
struct Request {
  std::array<char, MAX_URL + 1> url{};
  uint64_t ticket{0};
};

/** Single-owner request decisions, without network, pixels or allocation.
 * Call only while holding the worker state lock. A ticket is unique for each
 * attempt, including retries. Clearing a session invalidates all old tickets.
 */
class RequestPlan {
 public:
  /** Select a cover. Invalid input clears the old cover rather than reusing it. */
  bool select(std::string_view url) {
    if (!allowed_url(url)) url = {};
    if (url == std::string_view(url_.data())) return false;
    invalidate();
    url_.fill(0);
    if (!url.empty()) std::memcpy(url_.data(), url.data(), url.size());
    return true;
  }
  /** Logout always invalidates work, including a later identical album URL. */
  void clear() {
    invalidate();
    url_.fill(0);
  }
  /** Return an empty owned request while no download is needed. */
  Request begin(uint32_t now) {
    if (!url_[0] || ready_ || pending_ || permanent_ || exhausted_ ||
        (waiting_ && static_cast<uint32_t>(now - retry_started_) < retry_delay_)) return {};
    advance();
    if (exhausted_) return {};
    waiting_ = false;
    pending_ = true;
    return Request{url_, ticket_};
  }
  /** Accept a completed attempt only once. Clamp delays below half the clock. */
  bool finish(uint64_t ticket, bool success, uint32_t now, uint32_t delay_ms,
              bool permanent = false) {
    if (!pending_ || !ticket || ticket != ticket_) return false;
    pending_ = false;
    ready_ = success;
    if (success) {
      waiting_ = false;
      permanent_ = false;
      retry_count_ = 0;
      return true;
    }
    permanent_ = permanent;
    waiting_ = !permanent;
    if (permanent) {
      retry_count_ = 0;
      return true;
    }
    retry_started_ = now;
    retry_delay_ = this->scaled_delay_(delay_ms);
    if (retry_count_ < 8) ++retry_count_;
    return true;
  }
  bool ready() const { return ready_; }

 private:
  void advance() {
    if (ticket_ == std::numeric_limits<uint64_t>::max()) exhausted_ = true;
    else ++ticket_;
  }
  uint32_t scaled_delay_(uint32_t base) const {
    uint32_t delay = base < 1000 ? 1000 : (base > 300000 ? 300000 : base);
    for (uint8_t i = 0; i < retry_count_ && delay < 300000; ++i)
      delay = delay > 150000 ? 300000 : delay * 2U;
    return delay;
  }
  void invalidate() {
    advance();
    pending_ = false;
    ready_ = false;
    waiting_ = false;
    permanent_ = false;
    retry_count_ = 0;
  }
  std::array<char, MAX_URL + 1> url_{};
  uint64_t ticket_{0};
  uint32_t retry_started_{0};
  uint32_t retry_delay_{0};
  bool pending_{false};
  bool ready_{false};
  bool waiting_{false};
  bool permanent_{false};
  bool exhausted_{false};
  uint8_t retry_count_{0};
};
static_assert(sizeof(RequestPlan) <= 288, "Artwork request state must stay bounded");
}  // namespace usp_spotify_artwork
