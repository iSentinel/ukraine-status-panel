#pragma once
#include "session_vault.h"
#include <cstdint>
#include <string_view>

namespace usp_spotify_session {
/** Implemented by the auth adapter; every method rejects stale generations. */
class PersistenceBridge {
 public:
  virtual ~PersistenceBridge() = default;
  virtual LoadState load(uint32_t generation, Session *output) = 0;
  virtual bool save(uint32_t generation, std::string_view refresh,
                    std::string_view client, std::string_view scope) = 0;
  virtual bool clear(uint32_t generation) = 0;
};
}  // namespace usp_spotify_session
