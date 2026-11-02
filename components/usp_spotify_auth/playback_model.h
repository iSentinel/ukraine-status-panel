#pragma once

#include <ArduinoJson.h>
#include "control_actions.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace usp_spotify_playback {

constexpr uint8_t VOLUME_UNKNOWN = 255;
constexpr size_t MAX_ITEM_NAME = 160;
constexpr size_t MAX_ARTISTS = 192;
constexpr size_t MAX_CONTEXT_NAME = 128;
constexpr size_t MAX_DEVICE_NAME = 96;
constexpr size_t MAX_DEVICE_TYPE = 32;
constexpr size_t MAX_IMAGE_URL = 256;

enum class ItemType : uint8_t { NONE, TRACK, EPISODE, OTHER };
enum class RepeatState : uint8_t { OFF, CONTEXT, TRACK };
enum class ResponseKind : uint8_t { JSON, EMPTY, REAUTH, FORBIDDEN, RATE_LIMIT, RETRY };
struct Permission { bool known{false}; bool allowed{false}; };

/** Fixed-size presentation state. No provider-owned pointers survive parsing. */
struct PlaybackSnapshot {
  std::array<char, MAX_ITEM_NAME + 1> item_name{};
  std::array<char, MAX_ARTISTS + 1> artists{};
  std::array<char, MAX_CONTEXT_NAME + 1> context_name{};
  std::array<char, MAX_DEVICE_NAME + 1> device_name{};
  std::array<char, MAX_DEVICE_TYPE + 1> device_type{};
  std::array<char, MAX_IMAGE_URL + 1> image_url{};
  uint32_t progress_ms{0};
  uint32_t duration_ms{0};
  uint8_t volume_percent{VOLUME_UNKNOWN};
  ItemType type{ItemType::NONE};
  bool valid{false};
  bool has_item{false};
  bool is_playing{false};
  bool device_active{false};
  bool private_session{false};
  bool restricted{false};
  bool supports_volume{false};
  uint8_t control_allowed_mask{0};
  bool control_actions_known{false};
  bool mode_state_known{false};
  bool shuffle_state{false};
  RepeatState repeat_state{RepeatState::OFF};
  Permission seek{},shuffle{},repeat_context{},repeat_track{},transfer{};
};

inline ResponseKind classify_status(int status) {
  if (status == 200) return ResponseKind::JSON;
  if (status == 204) return ResponseKind::EMPTY;
  if (status == 401) return ResponseKind::REAUTH;
  if (status == 403) return ResponseKind::FORBIDDEN;
  if (status == 429) return ResponseKind::RATE_LIMIT;
  return ResponseKind::RETRY;
}

namespace detail {

inline size_t utf8_width(const unsigned char *text, size_t left) {
  if (!left) return 0;
  const unsigned char first = text[0];
  if (first < 0x80) return 1;
  if (first >= 0xC2 && first <= 0xDF) {
    return left >= 2 && (text[1] & 0xC0) == 0x80 ? 2 : 0;
  }
  if (first >= 0xE0 && first <= 0xEF) {
    if (left < 3 || (text[1] & 0xC0) != 0x80 || (text[2] & 0xC0) != 0x80) return 0;
    if ((first == 0xE0 && text[1] < 0xA0) || (first == 0xED && text[1] >= 0xA0)) return 0;
    return 3;
  }
  if (first >= 0xF0 && first <= 0xF4) {
    if (left < 4 || (text[1] & 0xC0) != 0x80 || (text[2] & 0xC0) != 0x80 ||
        (text[3] & 0xC0) != 0x80) return 0;
    if ((first == 0xF0 && text[1] < 0x90) || (first == 0xF4 && text[1] > 0x8F)) return 0;
    return 4;
  }
  return 0;
}

template<size_t N>
inline bool copy_utf8(std::array<char, N> &target, std::string_view source) {
  static_assert(N > 0);
  target.fill(0);
  size_t in = 0, out = 0;
  while (in < source.size()) {
    const auto *bytes = reinterpret_cast<const unsigned char *>(source.data() + in);
    const size_t width = utf8_width(bytes, source.size() - in);
    if (!width) { target.fill(0); return false; }
    if (out + width >= N) break;
    std::memcpy(target.data() + out, source.data() + in, width);
    in += width; out += width;
  }
  target[out] = 0;
  return true;
}

inline bool text(JsonVariantConst value, std::string_view *out) {
  if (!out || !value.is<const char *>()) return false;
  const char *raw = value.as<const char *>();
  if (!raw) return false;
  *out = raw;
  return true;
}

inline bool boolean(JsonVariantConst value, bool *out) {
  if (!out || !value.is<bool>()) return false;
  *out = value.as<bool>();
  return true;
}

inline bool nullable_u32(JsonVariantConst value, uint32_t *out) {
  if (!out) return false;
  if (value.isNull()) { *out = 0; return true; }
  if (!value.is<uint32_t>()) return false;
  *out = value.as<uint32_t>();
  return true;
}

inline bool image_url(JsonObjectConst image, std::string_view *out) {
  if (!out || image.isNull() || !text(image["url"], out)) return false;
  if (out->size() > MAX_IMAGE_URL || out->rfind("https://", 0) != 0) return false;
  for (unsigned char ch : *out) if (ch < 0x21 || ch > 0x7E) return false;
  return true;
}

inline bool image_dimension(JsonVariantConst value, uint32_t *out) {
  if (!out || !value.is<uint32_t>()) return false;
  *out = value.as<uint32_t>();
  return *out > 0;
}

inline uint32_t image_score(uint32_t width, uint32_t height) {
  constexpr uint32_t target = 300;
  const uint32_t edge = width > height ? width : height;
  return edge >= target ? edge - target : (target - edge) * 2U;
}

inline bool optional_image(JsonArrayConst images,
                           std::array<char, MAX_IMAGE_URL + 1> &out) {
  out.fill(0);
  if (images.isNull() || images.size() == 0) return true;
  std::string_view fallback;
  std::string_view best;
  uint32_t best_score = UINT32_MAX;
  for (JsonObjectConst image : images) {
    std::string_view url;
    if (!image_url(image, &url)) continue;
    if (fallback.empty()) fallback = url;
    uint32_t width = 0, height = 0;
    if (!image_dimension(image["width"], &width) ||
        !image_dimension(image["height"], &height)) continue;
    const uint32_t score = image_score(width, height);
    if (score < best_score) {
      best = url;
      best_score = score;
    }
  }
  return best.empty() ? (fallback.empty() || copy_utf8(out, fallback))
                      : copy_utf8(out, best);
}

inline bool parse_device(JsonVariantConst value, PlaybackSnapshot *out) {
  if (value.isNull()) return true;
  JsonObjectConst device = value.as<JsonObjectConst>();
  if (device.isNull()) return false;
  std::string_view name, type;
  if (!boolean(device["is_active"], &out->device_active) ||
      !boolean(device["is_private_session"], &out->private_session) ||
      !boolean(device["is_restricted"], &out->restricted) ||
      !boolean(device["supports_volume"], &out->supports_volume) ||
      !text(device["name"], &name) || !text(device["type"], &type) ||
      !copy_utf8(out->device_name, name) || !copy_utf8(out->device_type, type)) return false;
  JsonVariantConst volume = device["volume_percent"];
  if (volume.isNull()) { out->volume_percent = VOLUME_UNKNOWN; return true; }
  if (!volume.is<uint8_t>() || volume.as<uint8_t>() > 100) return false;
  out->volume_percent = volume.as<uint8_t>();
  return true;
}


inline Permission permission(JsonVariantConst value, const char *name) {
  if (!value.is<JsonObjectConst>()) return {};
  const JsonObjectConst actions=value.as<JsonObjectConst>();
  const bool supplied=usp_spotify_controls::contains(actions,"disallows");
  const bool nested=actions["disallows"].is<JsonObjectConst>();
  if (supplied && !nested) return {};
  const JsonObjectConst restrictions=nested?actions["disallows"].as<JsonObjectConst>():actions;
  bool allowed=false,flat=true;
  if (!usp_spotify_controls::restriction(restrictions,name,nested,&allowed)) return {};
  if (nested && !usp_spotify_controls::restriction(actions,name,true,&flat)) return {};
  return {nested || usp_spotify_controls::contains(restrictions,name),allowed&&flat};
}

inline bool advanced_state(JsonObjectConst root, PlaybackSnapshot *out) {
  const bool has_shuffle=usp_spotify_controls::contains(root,"shuffle_state");
  const bool has_repeat=usp_spotify_controls::contains(root,"repeat_state");
  if (!has_shuffle && !has_repeat) return true;
  if (!has_shuffle || !has_repeat || !root["shuffle_state"].is<bool>() ||
      !root["repeat_state"].is<const char *>()) return false;
  const char *raw=root["repeat_state"].as<const char *>();
  if (!raw) return false;
  const std::string_view repeat(raw);
  if (repeat=="off") out->repeat_state=RepeatState::OFF;
  else if (repeat=="context") out->repeat_state=RepeatState::CONTEXT;
  else if (repeat=="track") out->repeat_state=RepeatState::TRACK;
  else return false;
  out->shuffle_state=root["shuffle_state"].as<bool>();
  out->mode_state_known=true;
  return true;
}

inline void advanced_actions(JsonVariantConst actions, PlaybackSnapshot *out) {
  out->seek=permission(actions,"seeking");
  out->shuffle=permission(actions,"toggling_shuffle");
  out->repeat_context=permission(actions,"toggling_repeat_context");
  out->repeat_track=permission(actions,"toggling_repeat_track");
  out->transfer=permission(actions,"transferring_playback");
}

inline bool append_artist(std::array<char, MAX_ARTISTS + 1> &out, std::string_view name) {
  std::array<char, MAX_ARTISTS + 1> clean{};
  if (!copy_utf8(clean, name)) return false;
  const size_t used = std::strlen(out.data());
  const size_t add = std::strlen(clean.data());
  const std::string_view separator = used ? ", " : "";
  if (used + separator.size() >= out.size()) return true;
  size_t at = used;
  for (char ch : separator) out[at++] = ch;
  const size_t room = out.size() - at;
  std::array<char, MAX_ARTISTS + 1> tail{};
  if (!copy_utf8(tail, std::string_view(clean.data(), add))) return false;
  size_t copied = std::strlen(tail.data());
  if (copied >= room) {
    copied = room - 1;
    while (copied && (static_cast<unsigned char>(tail[copied]) & 0xC0) == 0x80) --copied;
  }
  std::memcpy(out.data() + at, tail.data(), copied);
  out[at + copied] = 0;
  return true;
}

inline bool parse_artists(JsonArrayConst artists, PlaybackSnapshot *out) {
  out->artists.fill(0);
  if (artists.isNull() || artists.size() == 0) return false;
  bool found = false;
  for (JsonObjectConst artist : artists) {
    std::string_view name;
    if (artist.isNull() || !text(artist["name"], &name)) return false;
    if (!append_artist(out->artists, name)) return false;
    found = true;
  }
  return found;
}

inline bool parse_track(JsonObjectConst item, PlaybackSnapshot *out) {
  std::string_view name, album_name;
  JsonObjectConst album = item["album"].as<JsonObjectConst>();
  if (!text(item["name"], &name) || !item["duration_ms"].is<uint32_t>() ||
      album.isNull() || !text(album["name"], &album_name) ||
      !copy_utf8(out->item_name, name) || !copy_utf8(out->context_name, album_name) ||
      !parse_artists(item["artists"].as<JsonArrayConst>(), out) ||
      !optional_image(album["images"].as<JsonArrayConst>(), out->image_url)) return false;
  out->duration_ms = item["duration_ms"].as<uint32_t>();
  out->type = ItemType::TRACK;
  return true;
}

inline bool parse_episode(JsonObjectConst item, PlaybackSnapshot *out) {
  std::string_view name, show_name;
  JsonObjectConst show = item["show"].as<JsonObjectConst>();
  if (!text(item["name"], &name) || !item["duration_ms"].is<uint32_t>() ||
      show.isNull() || !text(show["name"], &show_name) ||
      !copy_utf8(out->item_name, name) || !copy_utf8(out->context_name, show_name) ||
      !optional_image(item["images"].as<JsonArrayConst>(), out->image_url)) return false;
  out->duration_ms = item["duration_ms"].as<uint32_t>();
  out->type = ItemType::EPISODE;
  return true;
}

inline bool parse_item(JsonVariantConst value, PlaybackSnapshot *out) {
  if (value.isNull()) return true;
  JsonObjectConst item = value.as<JsonObjectConst>();
  std::string_view type;
  if (item.isNull() || !text(item["type"], &type)) return false;
  out->has_item = true;
  if (type == "track") return parse_track(item, out);
  if (type == "episode") return parse_episode(item, out);
  out->type = ItemType::OTHER;
  std::string_view name;
  if (text(item["name"], &name) && !copy_utf8(out->item_name, name)) return false;
  if (item["duration_ms"].is<uint32_t>()) out->duration_ms = item["duration_ms"].as<uint32_t>();
  return true;
}

}  // namespace detail

/** Parse only presentation fields from Spotify Get Playback State. */
inline bool parse_playback(JsonObjectConst root, PlaybackSnapshot *output) {
  if (!output) return false;
  PlaybackSnapshot next{};
  if (root.isNull() || !detail::boolean(root["is_playing"], &next.is_playing) ||
      !detail::nullable_u32(root["progress_ms"], &next.progress_ms) ||
      !detail::parse_device(root["device"], &next) ||
      !detail::parse_item(root["item"], &next)) return false;
  const auto actions=usp_spotify_controls::action_mask(root["actions"]);
  next.control_allowed_mask=actions.allowed;
  next.control_actions_known=actions.known;
  if (!detail::advanced_state(root,&next)) return false;
  detail::advanced_actions(root["actions"],&next);
  next.valid = true;
  *output = next;
  return true;
}

static_assert(sizeof(PlaybackSnapshot) <= 1024, "Playback snapshot must stay bounded");

}  // namespace usp_spotify_playback
