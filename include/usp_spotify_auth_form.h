#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace usp_spotify_auth_form {
constexpr size_t MAX_BODY = 4096;
enum Field : uint8_t { CLIENT = 1, REDIRECT = 2, STATE = 4, CODE = 8, ERROR = 16 };

/** Strict bounded decoder; field names remain literal and cannot hide duplicates. */
inline int hex(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

inline bool decode(char *first, char *last, std::string_view *value) {
  char *out = first;
  for (char *in = first; in < last; ++in) {
    unsigned char ch = *in;
    if (ch == '%') {
      if (last - in < 3 || hex(in[1]) < 0 || hex(in[2]) < 0) return false;
      ch = static_cast<unsigned char>((hex(in[1]) << 4) | hex(in[2]));
      in += 2;
    } else if (ch == '+') {
      ch = ' ';
    }
    if (ch < 0x20 || ch > 0x7E) return false;
    *out++ = static_cast<char>(ch);
  }
  *out = 0;
  *value = std::string_view(first, out - first);
  return true;
}

/** Views are valid only until the caller erases or replaces its body buffer. */
struct Form {
  uint8_t fields{0};
  std::string_view client_id, redirect_uri, state, code, error;

  bool parse(char *data, size_t size, size_t capacity) {
    *this = Form{};
    if (size > MAX_BODY || capacity <= size || (size && data == nullptr))
      return false;
    if (!size) return true;
    if (data[size - 1] == '&') return false;
    size_t offset = 0;
    while (offset < size) {
      size_t end = offset;
      while (end < size && data[end] != '&') ++end;
      auto *equal = static_cast<char *>(std::memchr(data + offset, '=', end - offset));
      if (equal == nullptr) return reject_();
      const std::string_view key(data + offset, equal - data - offset);
      std::string_view value;
      if (!decode(equal + 1, data + end, &value) || !assign_(key, value))
        return reject_();
      offset = end + 1;
    }
    return true;
  }

  bool begin_shape() const {
    return fields == (CLIENT | REDIRECT) && !client_id.empty() && !redirect_uri.empty();
  }

  bool callback_shape() const {
    return (fields == (STATE | CODE) && !state.empty() && !code.empty()) ||
           (fields == (STATE | ERROR) && !state.empty() && !error.empty());
  }

 private:
  bool reject_() { *this = Form{}; return false; }

  bool assign_(std::string_view key, std::string_view value) {
    uint8_t bit = 0;
    std::string_view *target = nullptr;
    if (key == "client_id") { bit = CLIENT; target = &client_id; }
    else if (key == "redirect_uri") { bit = REDIRECT; target = &redirect_uri; }
    else if (key == "state") { bit = STATE; target = &state; }
    else if (key == "code") { bit = CODE; target = &code; }
    else if (key == "error") { bit = ERROR; target = &error; }
    else return false;
    if (fields & bit) return false;
    fields |= bit;
    *target = value;
    return true;
  }
};
}  // namespace usp_spotify_auth_form
