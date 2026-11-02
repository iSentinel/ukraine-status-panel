#pragma once
#include <ArduinoJson.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace usp_spotify_devices {
constexpr size_t MAX_DEVICES = 8;
constexpr size_t MAX_DEVICE_ID = 128;
constexpr size_t MAX_DEVICE_NAME = 96;
constexpr size_t MAX_DEVICE_TYPE = 32;
constexpr uint8_t VOLUME_UNKNOWN = 255;

struct Device {
  std::array<char, MAX_DEVICE_ID + 1> id{};
  std::array<char, MAX_DEVICE_NAME + 1> name{};
  std::array<char, MAX_DEVICE_TYPE + 1> type{};
  uint8_t volume_percent{VOLUME_UNKNOWN};
  bool active{false};
  bool private_session{false};
  bool restricted{true};
  bool supports_volume{false};
  bool selectable{false};
  /** Restore the same defaults without a temporary Device object. */
  void clear() {
    id.fill(0);name.fill(0);type.fill(0);volume_percent=VOLUME_UNKNOWN;
    active=false;private_session=false;restricted=true;
    supports_volume=false;selectable=false;
  }
};

struct Catalog {
  std::array<Device, MAX_DEVICES> devices{};
  uint8_t count{0};
  bool valid{false};
  bool truncated{false};
  /** Catalogue resets also run from the small display task stack. */
  void clear() {
    for(auto &device:devices)device.clear();
    count=0;valid=false;truncated=false;
  }
};

inline size_t utf8_width(const unsigned char *text, size_t left) {
  if (!left) return 0;
  const auto first = text[0];
  if (first < 0x80) return 1;
  if (first >= 0xC2 && first <= 0xDF)
    return left >= 2 && (text[1] & 0xC0) == 0x80 ? 2 : 0;
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

template<size_t N> bool copy_utf8(std::array<char, N> &out, std::string_view source) {
  out.fill(0);
  size_t input = 0, output = 0;
  while (input < source.size()) {
    const auto *bytes = reinterpret_cast<const unsigned char *>(source.data() + input);
    const size_t width = utf8_width(bytes, source.size() - input);
    if (!width) { out.fill(0); return false; }
    if (output + width >= N) break;
    std::memcpy(out.data() + output, source.data() + input, width);
    input += width; output += width;
  }
  out[output] = 0;
  return true;
}

inline bool copy_id(std::array<char, MAX_DEVICE_ID + 1> &out, JsonVariantConst value) {
  out.fill(0);
  if (value.isNull()) return true;
  if (!value.is<const char *>()) return false;
  const char *raw = value.as<const char *>();
  if (!raw) return false;
  const size_t length = strnlen(raw, MAX_DEVICE_ID + 1);
  if (!length || length > MAX_DEVICE_ID) return false;
  for (size_t i = 0; i < length; ++i) {
    const auto ch = static_cast<unsigned char>(raw[i]);
    if (ch < 0x21 || ch > 0x7E) return false;
  }
  std::memcpy(out.data(), raw, length);
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

inline bool volume(JsonVariantConst value, uint8_t *out) {
  if (!out) return false;
  if (value.isNull()) { *out = VOLUME_UNKNOWN; return true; }
  if (!value.is<uint8_t>() || value.as<uint8_t>() > 100) return false;
  *out = value.as<uint8_t>();
  return true;
}

inline bool parse_device(JsonObjectConst object, Device *out) {
  if (!out || object.isNull()) return false;
  Device next{};
  std::string_view name, type;
  if (!copy_id(next.id, object["id"]) || !text(object["name"], &name) ||
      !text(object["type"], &type) || !copy_utf8(next.name, name) ||
      !copy_utf8(next.type, type) || !boolean(object["is_active"], &next.active) ||
      !boolean(object["is_private_session"], &next.private_session) ||
      !boolean(object["is_restricted"], &next.restricted) ||
      !boolean(object["supports_volume"], &next.supports_volume) ||
      !volume(object["volume_percent"], &next.volume_percent)) return false;
  next.selectable = next.id[0] && !next.restricted;
  *out = next;
  return true;
}

inline bool duplicate(const Catalog &catalog, const Device &device) {
  if (!device.id[0]) return false;
  for (uint8_t i = 0; i < catalog.count; ++i)
    if (catalog.devices[i].id[0] && std::strcmp(catalog.devices[i].id.data(), device.id.data()) == 0)
      return true;
  return false;
}

/** Parse only into worker-owned scratch; valid is set after the final item. */
inline bool parse_into(JsonObjectConst root, Catalog *out) {
  if (!out) return false;
  out->clear();
  const JsonArrayConst devices = root["devices"].as<JsonArrayConst>();
  if (root.isNull() || devices.isNull()) return false;
  for (JsonObjectConst object : devices) {
    Device device{};
    if (!parse_device(object,&device) || duplicate(*out,device)) return false;
    if (out->count < MAX_DEVICES) out->devices[out->count++]=device;
    else out->truncated=true;
  }
  out->valid=true;return true;
}
/** Legacy callers retain their previous output when parsing fails. */
inline bool parse(JsonObjectConst root, Catalog *out) {
  if (!out) return false;
  Catalog next{};
  if (!parse_into(root,&next)) return false;
  *out=next;return true;
}

static_assert(sizeof(Catalog) <= 2304, "Device catalog must stay bounded");
}  // namespace usp_spotify_devices
