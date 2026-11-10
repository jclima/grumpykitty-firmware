#pragma once
#include <cstdio>
#include <string>
#include <functional>

namespace infrared_stream {
/* Incremental stdio reader: 512-byte input buffer, at most 2048 source bytes
 * consumed per step.  The line is heap-backed so an 8KiB line never uses the
 * application task stack. */
class Reader {
 public:
  using ReadFn = size_t (*)(void*, size_t, size_t, FILE*);
  explicit Reader(FILE* f, size_t maxBytes = 4 * 1024 * 1024, ReadFn read = std::fread) : f_(f), read_(read), max_(maxBytes) {}
  bool step(const std::function<void(const char*)>& line, size_t budget = 2048) {
    if (done_ || failed_ || cancelled_) return true;
    size_t used = 0;
    while (used < budget) {
      if (pos_ == len_) { len_ = read_(buf_, 1, sizeof(buf_), f_); pos_ = 0;
        if (!len_) { if (std::ferror(f_)) { failed_ = true; return true; }
          if (!std::feof(f_)) { failed_ = true; return true; }
          if (line_.size()) { line(line_.c_str()); line_.clear(); }
          done_ = true; return true; } }
      const unsigned char c = buf_[pos_++]; ++used; ++bytes_;
      if (bytes_ > max_) { failed_ = true; return true; }
      if (c == '\n') { line(line_.c_str()); line_.clear(); }
      else { if (line_.size() >= 8192) { failed_ = true; return true; } line_.push_back((char)c); }
    }
    return false;
  }
  void cancel() { cancelled_ = true; line_.clear(); }
  bool done() const { return done_; }
  bool failed() const { return failed_; }
  size_t bytes() const { return bytes_; }
 private:
  FILE* f_; ReadFn read_; unsigned char buf_[512]{}; size_t pos_ = 0, len_ = 0, bytes_ = 0, max_ = 4 * 1024 * 1024; std::string line_;
  bool done_ = false, failed_ = false, cancelled_ = false;
};
}
