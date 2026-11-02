#pragma once

#include <mutex>
#include <string>

namespace esphome::web_server_base::internal {

// A request owns this snapshot until authentication returns. A password change
// cannot invalidate its strings, and no lock is held during authentication.
struct CredentialSnapshot {
  std::string username;
  std::string password;
  bool username_set{false};
  bool password_set{false};

  const char *username_value() const {
    return this->username_set ? this->username.c_str() : nullptr;
  }
  const char *password_value() const {
    return this->password_set ? this->password.c_str() : nullptr;
  }
};

class OwnedCredentials {
 public:
  // Copy while the caller's pointer is valid; never retain that pointer.
  void set_username(const char *value) {
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->value_.username = value != nullptr ? value : "";
    this->value_.username_set = value != nullptr;
  }

  void set_password(const char *value) {
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->value_.password = value != nullptr ? value : "";
    this->value_.password_set = value != nullptr;
  }

  bool is_set() const {
    std::lock_guard<std::mutex> lock(this->mutex_);
    return this->value_.username_set;
  }

  CredentialSnapshot snapshot() const {
    std::lock_guard<std::mutex> lock(this->mutex_);
    return this->value_;
  }

 protected:
  mutable std::mutex mutex_;
  CredentialSnapshot value_;
};

}  // namespace esphome::web_server_base::internal
