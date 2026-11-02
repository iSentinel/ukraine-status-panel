#pragma once

#include <cstddef>
#include <cstring>

namespace esphome::usp_outage {

class GroupObjectScanner {
 public:
  static constexpr size_t MAX_PATH = 5;
  static constexpr size_t MAX_KEY = 16;
  static constexpr size_t MAX_DEPTH = 12;

  GroupObjectScanner(const char *const *path, size_t path_len, char *output,
                     size_t capacity)
      : output_(output), capacity_(capacity), path_len_(path_len) {
    if (path == nullptr || output == nullptr || capacity == 0 || path_len == 0 ||
        path_len > MAX_PATH) {
      this->failed_ = true;
      return;
    }
    for (size_t i = 0; i < path_len; ++i) {
      if (!this->copy_key_(this->path_[i], path[i])) {
        this->failed_ = true;
        return;
      }
    }
  }

  bool consume(char ch) {
    if (this->failed_) return false;
    if (this->found_) return true;
    if (this->capturing_) return this->capture_(ch);
    return this->scan_(ch);
  }

  bool found() const { return this->found_; }
  bool failed() const { return this->failed_; }
  bool overflow() const { return this->overflow_; }
  size_t length() const { return this->length_; }

 private:
  static bool is_space_(char ch) {
    return ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t';
  }

  bool copy_key_(char *dest, const char *source) {
    if (source == nullptr) return false;
    const size_t length = std::strlen(source);
    if (length == 0 || length >= MAX_KEY) return false;
    std::memcpy(dest, source, length + 1);
    return true;
  }

  bool key_matches_path_() const {
    if (this->path_index_ >= this->path_len_) return false;
    if (this->object_depth_ != this->path_index_ + 1) return false;
    return std::strcmp(this->current_key_, this->path_[this->path_index_]) == 0;
  }

  void begin_object_() {
    if (this->object_depth_ >= MAX_DEPTH) {
      this->failed_ = true;
      return;
    }
    ++this->object_depth_;
    this->object_array_depth_[this->object_depth_] = this->array_depth_;
    this->expect_key_[this->object_depth_] = true;
  }

  void end_object_() {
    if (this->object_depth_ == 0) {
      this->failed_ = true;
      return;
    }
    --this->object_depth_;
    const size_t max_matched = this->object_depth_ == 0
                                   ? 0
                                   : this->object_depth_ - 1;
    if (this->path_index_ > max_matched) this->path_index_ = max_matched;
  }

  void start_string_(bool key) {
    this->in_string_ = true;
    this->reading_key_ = key;
    this->escaped_ = false;
    this->key_length_ = 0;
    this->key_overflow_ = false;
  }

  void finish_key_() {
    if (this->key_overflow_) {
      this->failed_ = true;
      return;
    }
    this->key_buffer_[this->key_length_] = '\0';
    std::memcpy(this->current_key_, this->key_buffer_, this->key_length_ + 1);
    this->have_key_ = true;
    this->expect_colon_ = true;
    this->expect_key_[this->object_depth_] = false;
  }

  bool consume_string_(char ch) {
    if (this->escaped_) {
      this->escaped_ = false;
    } else if (ch == '\\') {
      this->escaped_ = true;
    } else if (ch == '"') {
      this->in_string_ = false;
      if (this->reading_key_) this->finish_key_();
      this->reading_key_ = false;
      return !this->failed_;
    }
    if (this->reading_key_ && ch != '"' && !this->escaped_) {
      if (this->key_length_ + 1 < MAX_KEY) {
        this->key_buffer_[this->key_length_++] = ch;
      } else {
        this->key_overflow_ = true;
      }
    }
    return !this->failed_;
  }

  bool begin_value_object_() {
    const bool path_match = this->have_key_ && this->key_matches_path_();
    if (path_match && this->path_index_ + 1 == this->path_len_) {
      this->capturing_ = true;
      this->capture_depth_ = 0;
      return this->capture_('{');
    }
    if (path_match) ++this->path_index_;
    this->have_key_ = false;
    this->expect_value_ = false;
    this->begin_object_();
    return !this->failed_;
  }

  bool scan_(char ch) {
    if (this->in_string_) return this->consume_string_(ch);
    if (this->object_depth_ == 0) {
      if (is_space_(ch)) return true;
      if (ch != '{') {
        this->failed_ = true;
        return false;
      }
      this->begin_object_();
      return !this->failed_;
    }
    if (this->expect_colon_) {
      if (is_space_(ch)) return true;
      if (ch != ':') {
        this->failed_ = true;
        return false;
      }
      this->expect_colon_ = false;
      this->expect_value_ = true;
      return true;
    }
    if (this->expect_value_) {
      if (is_space_(ch)) return true;
      if (ch == '{') return this->begin_value_object_();
      this->have_key_ = false;
      this->expect_value_ = false;
      if (ch == '[') ++this->array_depth_;
      else if (ch == '"') this->start_string_(false);
      return true;
    }
    const bool object_level =
        this->array_depth_ == this->object_array_depth_[this->object_depth_];
    if (ch == '"') {
      this->start_string_(object_level && this->expect_key_[this->object_depth_]);
      return true;
    }
    if (ch == '{') {
      this->begin_object_();
      return !this->failed_;
    }
    if (ch == '}') {
      this->end_object_();
      return !this->failed_;
    }
    if (ch == '[') {
      ++this->array_depth_;
      return true;
    }
    if (ch == ']') {
      if (this->array_depth_ == 0) {
        this->failed_ = true;
        return false;
      }
      --this->array_depth_;
      return true;
    }
    if (ch == ',' && object_level) {
      this->expect_key_[this->object_depth_] = true;
      this->have_key_ = false;
    }
    return true;
  }

  bool capture_(char ch) {
    if (this->length_ >= this->capacity_) {
      this->overflow_ = true;
      this->failed_ = true;
      return false;
    }
    this->output_[this->length_++] = ch;
    if (this->capture_in_string_) {
      if (this->capture_escaped_) this->capture_escaped_ = false;
      else if (ch == '\\') this->capture_escaped_ = true;
      else if (ch == '"') this->capture_in_string_ = false;
      return true;
    }
    if (ch == '"') this->capture_in_string_ = true;
    else if (ch == '{') ++this->capture_depth_;
    else if (ch == '}' && --this->capture_depth_ == 0) {
      this->found_ = true;
      this->capturing_ = false;
    }
    return true;
  }

  char *output_{nullptr};
  size_t capacity_{0};
  char path_[MAX_PATH][MAX_KEY]{};
  size_t path_len_{0};
  size_t path_index_{0};
  size_t object_depth_{0};
  size_t array_depth_{0};
  size_t object_array_depth_[MAX_DEPTH + 1]{};
  bool expect_key_[MAX_DEPTH + 1]{};
  bool in_string_{false};
  bool reading_key_{false};
  bool escaped_{false};
  bool key_overflow_{false};
  char key_buffer_[MAX_KEY]{};
  size_t key_length_{0};
  char current_key_[MAX_KEY]{};
  bool have_key_{false};
  bool expect_colon_{false};
  bool expect_value_{false};
  bool capturing_{false};
  bool capture_in_string_{false};
  bool capture_escaped_{false};
  int capture_depth_{0};
  size_t length_{0};
  bool overflow_{false};
  bool found_{false};
  bool failed_{false};
};

}  // namespace esphome::usp_outage
