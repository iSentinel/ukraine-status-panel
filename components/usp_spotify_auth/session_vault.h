#pragma once
#include "usp_spotify_tokens.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace usp_spotify_session {
constexpr uint32_t MAGIC = 0x53505553U;  // "SUPS" little-endian marker.
constexpr uint16_t VERSION = 1;
constexpr uint8_t FLAG_PRESENT = 1;
constexpr size_t NONCE_BYTES = 12;
constexpr size_t TAG_BYTES = 16;
constexpr size_t MAX_SCOPE = 512;

inline void wipe(void *data, size_t size) {
  volatile uint8_t *bytes = static_cast<volatile uint8_t *>(data);
  while (size--) *bytes++ = 0;
}

struct Session {
  std::array<char, usp_spotify_tokens::MAX_TOKEN + 1> refresh{};
  std::array<char, usp_spotify_pkce::MAX_CLIENT_ID + 1> client{};
  std::array<char, MAX_SCOPE + 1> scope{};
  ~Session() { clear(); }
  void clear() { wipe(refresh.data(), refresh.size()); wipe(client.data(), client.size()); wipe(scope.data(), scope.size()); }
};

struct Plaintext {
  uint16_t refresh_len{0}, client_len{0}, scope_len{0};
  std::array<char, usp_spotify_tokens::MAX_TOKEN> refresh{};
  std::array<char, usp_spotify_pkce::MAX_CLIENT_ID> client{};
  std::array<char, MAX_SCOPE> scope{};
};

struct Header {
  uint32_t magic{MAGIC};
  uint16_t version{VERSION};
  uint8_t key_id{0};
  uint8_t flags{FLAG_PRESENT};
};

struct Record {
  Header header{};
  std::array<uint8_t, NONCE_BYTES> nonce{};
  std::array<uint8_t, sizeof(Plaintext)> ciphertext{};
  std::array<uint8_t, TAG_BYTES> tag{};
};

static_assert(std::is_trivially_copyable_v<Record> && sizeof(Record) < 4096);

/** Caller-owned external-memory workspace; no large record is placed on a task stack. */
struct Workspace {
  Record record{};
  Plaintext plain{};
  Workspace()=default;
  Workspace(const Workspace &)=delete;
  Workspace &operator=(const Workspace &)=delete;
  void clear() { wipe(this,sizeof(*this)); }
  ~Workspace() { clear(); }
};

/** Clear plaintext and ciphertext on every success and failure path. */
class WorkspaceUse {
 public:
  explicit WorkspaceUse(Workspace &workspace):workspace_(workspace) { workspace_.clear(); }
  ~WorkspaceUse() { workspace_.clear(); }
  WorkspaceUse(const WorkspaceUse &)=delete;
  WorkspaceUse &operator=(const WorkspaceUse &)=delete;
 private:
  Workspace &workspace_;
};

enum class KeyState : uint8_t { READY, UNPROVISIONED, WRONG_PURPOSE, ERROR };
enum class LoadState : uint8_t { EMPTY, READY, KEY_UNAVAILABLE, CORRUPT, IO_ERROR };

class Storage {
 public:
  virtual ~Storage() = default;
  virtual bool load(Record *record) = 0;
  virtual bool save(const Record &record) = 0;
  virtual bool io_error() const { return false; }
};

class Crypto {
 public:
  virtual ~Crypto() = default;
  virtual KeyState key_state(uint8_t key_id) = 0;
  virtual bool nonce(uint8_t *output, size_t size) = 0;
  virtual bool seal(uint8_t key_id, const uint8_t *nonce, const Header &header,
                    const Plaintext &plain, Record *record) = 0;
  virtual bool open(uint8_t key_id, const Record &record, Plaintext *plain) = 0;
};

inline bool scope_value(std::string_view value) {
  if (value.size() > MAX_SCOPE) return false;
  for (unsigned char ch : value) if (ch < 0x20 || ch > 0x7e) return false;
  return true;
}

inline bool copy_bytes(char *output, size_t capacity, std::string_view value) {
  if (value.size() >= capacity) return false;
  std::memcpy(output, value.data(), value.size());
  output[value.size()] = 0;
  return true;
}

inline bool pack(std::string_view refresh, std::string_view client,
                 std::string_view scope, Plaintext *output) {
  if (!output || !usp_spotify_tokens::opaque_value(refresh) ||
      !usp_spotify_pkce::valid_client_id(client) || !scope_value(scope)) return false;
  wipe(output,sizeof(*output));
  output->refresh_len=refresh.size();output->client_len=client.size();output->scope_len=scope.size();
  std::memcpy(output->refresh.data(),refresh.data(),refresh.size());
  std::memcpy(output->client.data(),client.data(),client.size());
  std::memcpy(output->scope.data(),scope.data(),scope.size());
  return true;
}

inline bool unpack(const Plaintext &plain, Session *output) {
  if (!output || plain.refresh_len == 0 || plain.refresh_len > plain.refresh.size() ||
      plain.client_len == 0 || plain.client_len > plain.client.size() ||
      plain.scope_len > plain.scope.size()) return false;
  const std::string_view refresh(plain.refresh.data(), plain.refresh_len);
  const std::string_view client(plain.client.data(), plain.client_len);
  const std::string_view scope(plain.scope.data(), plain.scope_len);
  if (!usp_spotify_tokens::opaque_value(refresh) || !usp_spotify_pkce::valid_client_id(client) ||
      !scope_value(scope)) return false;
  output->clear();
  std::memcpy(output->refresh.data(),refresh.data(),refresh.size());
  std::memcpy(output->client.data(),client.data(),client.size());
  std::memcpy(output->scope.data(),scope.data(),scope.size());
  return true;
}

class Vault {
 public:
  Vault(Storage &storage, Crypto &crypto) : storage_(storage), crypto_(crypto) {}
  Vault(Storage &storage, Crypto &crypto, uint8_t key_id)
      : storage_(storage), crypto_(crypto), key_id_(key_id), key_configured_(true) {}
  /** Bind once before worker startup; the caller retains workspace ownership. */
  void set_workspace(Workspace *workspace) { workspace_=workspace; }
  void set_key_id(uint8_t key_id) { key_id_=key_id;key_configured_=true; }

  bool store(std::string_view refresh, std::string_view client, std::string_view scope) {
    if (!workspace_ || !key_configured_ || crypto_.key_state(key_id_) != KeyState::READY) return false;
    WorkspaceUse use(*workspace_);
    auto &plain=workspace_->plain;auto &record=workspace_->record;
    record.header=Header{};record.header.key_id=key_id_;
    if (!pack(refresh, client, scope, &plain) || !crypto_.nonce(record.nonce.data(), record.nonce.size()) ||
        !crypto_.seal(key_id_, record.nonce.data(), record.header, plain, &record)) {
      wipe(&plain, sizeof(plain)); return false;
    }
    wipe(&plain, sizeof(plain));
    return storage_.save(record);
  }

  LoadState load(Session *output) {
    if (!output) return LoadState::IO_ERROR;
    output->clear();
    if (!workspace_) return LoadState::IO_ERROR;
    WorkspaceUse use(*workspace_);auto &record=workspace_->record;
    if (!storage_.load(&record)) return storage_.io_error()?LoadState::IO_ERROR:LoadState::EMPTY;
    if (record.header.magic == 0 && record.header.flags == 0) return LoadState::EMPTY;
    if (!key_configured_) return LoadState::KEY_UNAVAILABLE;
    if (record.header.magic != MAGIC || record.header.version != VERSION ||
        record.header.flags != FLAG_PRESENT || record.header.key_id != key_id_)
      return LoadState::CORRUPT;
    if (crypto_.key_state(key_id_) != KeyState::READY) return LoadState::KEY_UNAVAILABLE;
    auto &plain=workspace_->plain;
    if (!crypto_.open(key_id_, record, &plain)) { wipe(&plain, sizeof(plain)); return LoadState::CORRUPT; }
    const bool ok = unpack(plain, output); wipe(&plain, sizeof(plain));
    return ok ? LoadState::READY : LoadState::CORRUPT;
  }

  bool clear() {
    if (!workspace_) return false;
    WorkspaceUse use(*workspace_);auto &tombstone=workspace_->record;
    tombstone.header = {}; tombstone.header.magic = 0; tombstone.header.version = 0;
    tombstone.header.key_id = key_id_; tombstone.header.flags = 0;
    return storage_.save(tombstone);
  }

 private:
  Storage &storage_; Crypto &crypto_; Workspace *workspace_{nullptr};
  uint8_t key_id_{0};bool key_configured_{false};
};
}  // namespace usp_spotify_session
