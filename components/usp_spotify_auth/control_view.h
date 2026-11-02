#pragma once
#include "control_plan.h"

namespace usp_spotify_controls {
/** Public UI value contains only permission bits and command status. */
struct ControlView {
  uint8_t allowed_mask{0};
  Phase phase{Phase::IDLE};
  Outcome outcome{Outcome::NONE};
};
}
