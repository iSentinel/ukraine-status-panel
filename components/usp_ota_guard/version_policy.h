#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <string>

namespace esphome::usp_ota_version {

/** A validated version; build metadata does not affect its precedence. */
struct Version {
  std::array<uint32_t, 3> core{};
  std::string prerelease;
};

/** Accept only ASCII characters permitted in one version identifier. */
inline bool identifier_character(char value) {
  return (value >= '0' && value <= '9') ||
         (value >= 'a' && value <= 'z') ||
         (value >= 'A' && value <= 'Z') || value == '-';
}

/** Check dot-separated identifiers; numeric prereleases cannot start with zero. */
inline bool valid_identifiers(const std::string &value, bool prerelease) {
  if (value.empty()) return false;
  size_t start = 0;
  for (size_t end = 0; end <= value.size(); ++end) {
    if (end != value.size() && value[end] != '.') {
      if (!identifier_character(value[end])) return false;
      continue;
    }
    if (end == start) return false;
    bool numeric = true;
    for (size_t index = start; index < end; ++index)
      numeric &= value[index] >= '0' && value[index] <= '9';
    if (prerelease && numeric && end - start > 1 && value[start] == '0')
      return false;
    start = end + 1;
  }
  return true;
}

/** Read exactly three nonnegative core numbers with no leading zero or overflow. */
inline bool parse_core(const std::string &value, size_t &position,
                       std::array<uint32_t, 3> &core) {
  for (size_t field = 0; field < core.size(); ++field) {
    size_t start = position;
    uint64_t number = 0;
    while (position < value.size() &&
           value[position] >= '0' && value[position] <= '9') {
      number = number * 10U + static_cast<uint32_t>(value[position++] - '0');
      if (number > std::numeric_limits<uint32_t>::max()) return false;
    }
    if (position == start ||
        (position - start > 1 && value[start] == '0')) return false;
    core[field] = static_cast<uint32_t>(number);
    if (field + 1 < core.size()) {
      if (position == value.size() || value[position++] != '.') return false;
    }
  }
  return true;
}

/** Validate the whole version, including optional prerelease and build metadata. */
inline bool parse(const std::string &value, Version &output) {
  Version parsed;
  size_t position = 0;
  if (!parse_core(value, position, parsed.core)) return false;
  if (position < value.size() && value[position] == '-') {
    size_t start = ++position;
    while (position < value.size() && value[position] != '+') ++position;
    parsed.prerelease = value.substr(start, position - start);
    if (!valid_identifiers(parsed.prerelease, true)) return false;
  }
  if (position < value.size() && value[position] == '+') {
    if (!valid_identifiers(value.substr(position + 1), false)) return false;
    position = value.size();
  }
  if (position != value.size()) return false;
  output = parsed;
  return true;
}

/** Identify numeric prerelease tokens without converting arbitrarily long digits. */
inline bool numeric_identifier(const std::string &value) {
  for (char character : value)
    if (character < '0' || character > '9') return false;
  return !value.empty();
}

/** Compare one validated prerelease token, treating numbers numerically. */
inline int compare_identifier(const std::string &left, const std::string &right) {
  bool left_numeric = numeric_identifier(left);
  bool right_numeric = numeric_identifier(right);
  if (left_numeric != right_numeric) return left_numeric ? -1 : 1;
  if (left_numeric && left.size() != right.size())
    return left.size() < right.size() ? -1 : 1;
  int result = left.compare(right);
  return (result > 0) - (result < 0);
}

/** Return one identifier and advance beyond the dot or the end of the string. */
inline std::string next_identifier(const std::string &value, size_t &position) {
  size_t end = value.find('.', position);
  if (end == std::string::npos) end = value.size();
  std::string result = value.substr(position, end - position);
  position = end + 1;
  return result;
}

/** A release follows its prereleases; longer matching identifier lists follow. */
inline int compare_prerelease(const std::string &left, const std::string &right) {
  if (left.empty() || right.empty())
    return left.empty() ? (right.empty() ? 0 : 1) : -1;
  size_t left_position = 0, right_position = 0;
  while (left_position <= left.size() && right_position <= right.size()) {
    std::string left_id = next_identifier(left, left_position);
    std::string right_id = next_identifier(right, right_position);
    int result = compare_identifier(left_id, right_id);
    if (result != 0) return result;
  }
  return (left_position <= left.size()) - (right_position <= right.size());
}

/** Compare validated versions; build metadata is intentionally absent here. */
inline int compare(const Version &left, const Version &right) {
  for (size_t index = 0; index < left.core.size(); ++index) {
    if (left.core[index] != right.core[index])
      return left.core[index] < right.core[index] ? -1 : 1;
  }
  return compare_prerelease(left.prerelease, right.prerelease);
}

/** Reject malformed versions even when an explicit downgrade override is set. */
inline bool allowed(const std::string &target, const std::string &current,
                    bool allow_downgrade) {
  Version target_version, current_version;
  if (!parse(target, target_version) || !parse(current, current_version))
    return false;
  return allow_downgrade || compare(target_version, current_version) >= 0;
}

}  // namespace esphome::usp_ota_version
