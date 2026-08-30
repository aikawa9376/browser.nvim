#pragma once

#include "json.h"

#include <istream>
#include <mutex>
#include <ostream>
#include <string>

namespace browser {

class Ipc {
 public:
  Ipc(std::istream& input, std::ostream& output)
      : input_(input), output_(output) {}

  enum class ReadResult { kMessage, kEnd, kError };

  ReadResult Read(JsonValue* message, std::string* error);
  bool Write(const JsonValue& message, std::string* error = nullptr);

 private:
  std::istream& input_;
  std::ostream& output_;
  std::mutex output_mutex_;
};

}  // namespace browser
