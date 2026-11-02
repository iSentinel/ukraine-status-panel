#include "playback_model.h"
#include <ArduinoJson.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

using namespace usp_spotify_playback;

static JsonDocument parse(const std::string &text) {
  JsonDocument document;
  const auto error = deserializeJson(document, text);
  assert(!error);
  return document;
}

static bool valid_utf8(const char *text) {
  const auto *p = reinterpret_cast<const unsigned char *>(text);
  while (*p) {
    if (*p < 0x80) { ++p; continue; }
    unsigned need = (*p & 0xE0) == 0xC0 ? 1 : (*p & 0xF0) == 0xE0 ? 2 : (*p & 0xF8) == 0xF0 ? 3 : 9;
    if (need == 9) return false;
    ++p;
    for (unsigned i = 0; i < need; ++i, ++p) if ((*p & 0xC0) != 0x80) return false;
  }
  return true;
}

static void track_case() {
  auto doc = parse(R"({"device":{"is_active":true,"is_private_session":false,"is_restricted":false,"name":"Kitchen","type":"Speaker","volume_percent":59,"supports_volume":true},"progress_ms":12345,"is_playing":true,"item":{"type":"track","name":"Пісня 🎵","duration_ms":201000,"artists":[{"name":"Артист A"},{"name":"Artist B"}],"album":{"name":"Альбом","images":[{"url":"https://i.scdn.co/image/cover"}]}},"currently_playing_type":"track"})");
  PlaybackSnapshot out{};
  assert(parse_playback(doc.as<JsonObjectConst>(), &out));
  assert(out.valid && out.has_item && out.type == ItemType::TRACK && out.is_playing);
  assert(out.progress_ms == 12345 && out.duration_ms == 201000);
  assert(std::strcmp(out.device_name.data(), "Kitchen") == 0 && out.volume_percent == 59);
  assert(std::strcmp(out.item_name.data(), "Пісня 🎵") == 0);
  assert(std::strstr(out.artists.data(), "Артист A") && std::strstr(out.artists.data(), "Artist B"));
  assert(std::strcmp(out.context_name.data(), "Альбом") == 0);
  assert(std::strcmp(out.image_url.data(), "https://i.scdn.co/image/cover") == 0);
}

static void episode_and_empty_cases() {
  auto episode = parse(R"({"device":{"is_active":true,"is_private_session":false,"is_restricted":false,"name":"Phone","type":"Smartphone","volume_percent":null,"supports_volume":false},"progress_ms":0,"is_playing":false,"item":{"type":"episode","name":"Episode","duration_ms":3600000,"images":[{"url":"https://i.scdn.co/image/e"}],"show":{"name":"Show"}},"currently_playing_type":"episode"})");
  PlaybackSnapshot out{};
  assert(parse_playback(episode.as<JsonObjectConst>(), &out));
  assert(out.type == ItemType::EPISODE && out.volume_percent == VOLUME_UNKNOWN);
  assert(std::strcmp(out.context_name.data(), "Show") == 0);
  auto empty = parse(R"({"device":null,"progress_ms":null,"is_playing":false,"item":null,"currently_playing_type":"unknown"})");
  assert(parse_playback(empty.as<JsonObjectConst>(), &out));
  assert(out.valid && !out.has_item && !out.is_playing && out.progress_ms == 0);
}

static void schema_and_future_cases() {
  PlaybackSnapshot out{};
  auto future = parse(R"({"device":null,"progress_ms":42,"is_playing":true,"item":{"type":"audiobook","name":"Future","duration_ms":99},"currently_playing_type":"unknown"})");
  assert(parse_playback(future.as<JsonObjectConst>(), &out));
  assert(out.has_item && out.type == ItemType::OTHER);
  auto bad_volume = parse(R"({"device":{"is_active":true,"is_private_session":false,"is_restricted":false,"name":"X","type":"Y","volume_percent":101,"supports_volume":true},"progress_ms":0,"is_playing":false,"item":null})");
  assert(!parse_playback(bad_volume.as<JsonObjectConst>(), &out));
  auto missing = parse(R"({"device":null,"progress_ms":0,"item":null})");
  assert(!parse_playback(missing.as<JsonObjectConst>(), &out));
  auto bad_progress = parse(R"({"device":null,"progress_ms":-1,"is_playing":false,"item":null})");
  assert(!parse_playback(bad_progress.as<JsonObjectConst>(), &out));
}

static void bounded_utf8_and_image_cases() {
  std::string long_name;
  for (int i = 0; i < 200; ++i) long_name += "é";
  JsonDocument doc;
  doc["device"] = nullptr; doc["progress_ms"] = 999999; doc["is_playing"] = true;
  doc["currently_playing_type"] = "track";
  auto item = doc["item"].to<JsonObject>(); item["type"]="track"; item["name"]=long_name;
  item["duration_ms"] = 10; auto artists=item["artists"].to<JsonArray>(); artists.add<JsonObject>()["name"]="A";
  auto album=item["album"].to<JsonObject>(); album["name"]="B"; album["images"].to<JsonArray>().add<JsonObject>()["url"]="http://not-secure";
  PlaybackSnapshot out{};
  assert(parse_playback(doc.as<JsonObjectConst>(), &out));
  assert(valid_utf8(out.item_name.data()) && std::strlen(out.item_name.data()) < out.item_name.size());
  assert(out.progress_ms == 999999 && out.duration_ms == 10);  // UI clamps only presentation.
  assert(out.image_url[0] == 0);  // Metadata fetch never downgrades to plaintext.
}

static void image_selection_cases() {
  JsonDocument doc;
  auto images = doc["images"].to<JsonArray>();
  auto large = images.add<JsonObject>();
  large["url"] = "https://i.scdn.co/image/x640";
  large["width"] = 640;
  large["height"] = 640;
  auto target = images.add<JsonObject>();
  target["url"] = "https://i.scdn.co/image/x300";
  target["width"] = 300;
  target["height"] = 300;
  auto small = images.add<JsonObject>();
  small["url"] = "https://i.scdn.co/image/x64";
  small["width"] = 64;
  small["height"] = 64;
  std::array<char, MAX_IMAGE_URL + 1> output{};
  assert(usp_spotify_playback::detail::optional_image(images, output));
  assert(std::strcmp(output.data(), "https://i.scdn.co/image/x300") == 0);

  JsonDocument fallback_doc;
  auto fallback = fallback_doc["images"].to<JsonArray>();
  fallback.add<JsonObject>()["url"] = "https://i.scdn.co/image/first";
  fallback.add<JsonObject>()["url"] = "https://i.scdn.co/image/second";
  assert(usp_spotify_playback::detail::optional_image(fallback, output));
  assert(std::strcmp(output.data(), "https://i.scdn.co/image/first") == 0);
}

static void response_cases() {
  assert(classify_status(200) == ResponseKind::JSON);
  assert(classify_status(204) == ResponseKind::EMPTY);
  assert(classify_status(401) == ResponseKind::REAUTH);
  assert(classify_status(403) == ResponseKind::FORBIDDEN);
  assert(classify_status(429) == ResponseKind::RATE_LIMIT);
  assert(classify_status(500) == ResponseKind::RETRY);
}

int main() {
  track_case(); episode_and_empty_cases(); schema_and_future_cases();
  bounded_utf8_and_image_cases(); image_selection_cases(); response_cases();
  static_assert(sizeof(PlaybackSnapshot) <= 1024);
  std::cout << "SPOTIFY_PLAYBACK_MODEL_NATIVE=PASS track episode empty future utf8 status bounded\n";
}
