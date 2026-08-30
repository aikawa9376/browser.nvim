#pragma once

#include "cef_browser_manager.h"
#include "ipc.h"
#include "kitty_renderer.h"

#include <atomic>
#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <thread>

namespace browser {

class CefDaemon final : public CefEventSink {
 public:
  CefDaemon(std::istream& input, std::ostream& output);
  ~CefDaemon() override;

  int Run();

  void Emit(JsonValue event) override;
  void OnBrowserCountChanged() override;

 private:
  void ReaderLoop();
  void Handle(JsonValue message);
  void RequestShutdown();
  void Error(std::string message, std::uint32_t browser_id = 0);
  void ModeChanged(const CefBrowserState& state);
  CefBrowserState* StateFor(const JsonValue& message, std::string* error);

  KittyRenderer renderer_;
  CefBrowserManager manager_;
  Ipc ipc_;
  std::thread reader_thread_;
  std::atomic<bool> shutting_down_{false};
};

}  // namespace browser
