#pragma once

#include "browser_manager.h"
#include "ipc.h"
#include "kitty_renderer.h"

#include <istream>
#include <ostream>
#include <string>

namespace browser {

class App {
 public:
  App(bool dry_run, std::istream& input, std::ostream& output);
  int Run();

 private:
  bool Handle(const JsonValue& message);
  void Error(std::string message, std::uint32_t browser_id = 0);
  void ModeChanged(const BrowserState& state);
  BrowserState* StateFor(const JsonValue& message, std::string* error);

  KittyRenderer renderer_;
  BrowserManager manager_;
  Ipc ipc_;
  bool shutting_down_ = false;
};

}  // namespace browser
