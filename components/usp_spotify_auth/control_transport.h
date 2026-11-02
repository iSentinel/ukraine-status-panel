#pragma once
#include "control_plan.h"
#include <string_view>

namespace esphome::usp_spotify_auth {
/** Worker callback confirms the same session and current command permissions. */
using ControlCurrent = bool (*)(void *, const usp_spotify_controls::Request &);

/** One explicit loan, one fixed HTTPS request; no automatic command replay. */
class IdfControlTransport {
 public:
  static bool available();
  usp_spotify_controls::Response perform(
      const usp_spotify_controls::Request &request, std::string_view token,
      ControlCurrent current, void *context);
};
}  // namespace esphome::usp_spotify_auth
