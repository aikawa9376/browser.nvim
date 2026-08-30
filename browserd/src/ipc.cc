#include "ipc.h"

namespace browser {

Ipc::ReadResult Ipc::Read(JsonValue* message, std::string* error) {
  std::string line;
  if (!std::getline(input_, line)) {
    return input_.eof() ? ReadResult::kEnd : ReadResult::kError;
  }
  auto parsed = JsonValue::Parse(line, error);
  if (!parsed || !parsed->is_object()) {
    if (parsed && error) {
      *error = "top-level JSON value must be an object";
    }
    return ReadResult::kError;
  }
  *message = std::move(*parsed);
  return ReadResult::kMessage;
}

bool Ipc::Write(const JsonValue& message, std::string* error) {
  const std::lock_guard lock(output_mutex_);
  output_ << message.Dump() << '\n';
  output_.flush();
  if (!output_) {
    if (error) {
      *error = "failed to write IPC response";
    }
    return false;
  }
  return true;
}

}  // namespace browser
