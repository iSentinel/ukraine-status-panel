#pragma once

#include <cstdint>
#include <string_view>

namespace esphome::web_server_base::origin_policy {

/** Host spelling and port of one HTTP authority; input remains caller-owned. */
struct Authority {
  std::string_view host;
  uint16_t port{80};
};

/** Fold ASCII letters without locale-dependent conversions. */
inline char lower_ascii(char value) {
  return value >= 'A' && value <= 'Z' ? static_cast<char>(value + 'a' - 'A') : value;
}

/** Host names and the HTTP scheme are case insensitive. */
inline bool equal_ascii(std::string_view left, std::string_view right) {
  if (left.size() != right.size()) return false;
  for (size_t index = 0; index < left.size(); ++index)
    if (lower_ascii(left[index]) != lower_ascii(right[index])) return false;
  return true;
}

/** Accept the simple ASCII host spelling serialized by local browsers. */
inline bool valid_host(std::string_view value) {
  if (value.empty()) return false;
  for (char character : value) {
    bool alphanumeric = (character >= '0' && character <= '9') ||
                        (character >= 'A' && character <= 'Z') ||
                        (character >= 'a' && character <= 'z');
    if (!alphanumeric && character != '.' && character != '-' &&
        character != '_') return false;
  }
  return true;
}

/** Keep bracketed address text bounded to IPv6 address characters. */
inline bool valid_ip_literal(std::string_view value) {
  if (value.empty() || value.find(':') == std::string_view::npos) return false;
  for (char character : value) {
    bool hexadecimal = (character >= '0' && character <= '9') ||
                       (character >= 'a' && character <= 'f') ||
                       (character >= 'A' && character <= 'F');
    if (!hexadecimal && character != ':' && character != '.') return false;
  }
  return true;
}

/** Parse a nonzero TCP port with overflow checked after every digit. */
inline bool parse_port(std::string_view value, uint16_t &port) {
  if (value.empty()) return false;
  uint32_t number = 0;
  for (char character : value) {
    if (character < '0' || character > '9') return false;
    number = number * 10U + static_cast<uint32_t>(character - '0');
    if (number > 65535U) return false;
  }
  if (number == 0) return false;
  port = static_cast<uint16_t>(number);
  return true;
}

/** Split a host and optional port; reject userinfo, paths and header lists. */
inline bool parse_authority(std::string_view value, Authority &output) {
  if (value.empty() || value.size() > 512U) return false;
  Authority parsed;
  size_t position;
  if (value.front() == '[') {
    size_t closing = value.find(']');
    if (closing == std::string_view::npos ||
        !valid_ip_literal(value.substr(1, closing - 1))) return false;
    parsed.host = value.substr(0, closing + 1);
    position = closing + 1;
  } else {
    position = value.find(':');
    if (position == std::string_view::npos) position = value.size();
    parsed.host = value.substr(0, position);
    if (!valid_host(parsed.host)) return false;
  }
  if (position < value.size()) {
    if (value[position] != ':' ||
        !parse_port(value.substr(position + 1), parsed.port)) return false;
  }
  output = parsed;
  return true;
}

/** Match one local HTTP Origin against Host, including the effective port. */
inline bool matches(std::string_view origin, std::string_view host) {
  constexpr std::string_view scheme{"http://"};
  if (origin.size() < scheme.size() ||
      !equal_ascii(origin.substr(0, scheme.size()), scheme)) return false;
  Authority page, request;
  if (!parse_authority(origin.substr(scheme.size()), page) ||
      !parse_authority(host, request)) return false;
  return page.port == request.port && equal_ascii(page.host, request.host);
}

}  // namespace esphome::web_server_base::origin_policy
