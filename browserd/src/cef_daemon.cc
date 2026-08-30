#include "cef_daemon.h"

#include "include/cef_app.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"

#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

namespace browser {
namespace {

class LambdaTask final : public CefTask {
 public:
  explicit LambdaTask(std::function<void()> callback)
      : callback_(std::move(callback)) {}
  void Execute() override { callback_(); }

 private:
  std::function<void()> callback_;
  IMPLEMENT_REFCOUNTING(LambdaTask);
};

void PostToUi(std::function<void()> callback) {
  CefPostTask(TID_UI, new LambdaTask(std::move(callback)));
}

JsonValue::Object Event(std::string type) {
  JsonValue::Object event;
  event.emplace("type", JsonValue(std::move(type)));
  return event;
}

std::optional<std::uint32_t> BrowserId(const JsonValue& message) {
  const auto value = message.Integer("browser_id");
  if (!value || *value < 1 ||
      *value > static_cast<std::int64_t>(
                   std::numeric_limits<std::uint32_t>::max())) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(*value);
}

}  // namespace

CefDaemon::CefDaemon(std::istream& input, std::ostream& output)
    : manager_(&renderer_, this), ipc_(input, output) {}

CefDaemon::~CefDaemon() {
  if (reader_thread_.joinable()) {
    reader_thread_.join();
  }
}

int CefDaemon::Run() {
  CEF_REQUIRE_UI_THREAD();
  if (!renderer_.ready()) {
    std::cerr << "browserd: " << renderer_.error() << '\n';
    return 1;
  }

  const TerminalMetrics& metrics = renderer_.metrics();
  JsonValue::Object event = Event("terminal_metrics");
  event.emplace("cell_width", JsonValue(metrics.cell_width));
  event.emplace("cell_height", JsonValue(metrics.cell_height));
  event.emplace("columns", JsonValue(metrics.columns));
  event.emplace("rows", JsonValue(metrics.rows));
  event.emplace("pixel_width", JsonValue(metrics.pixel_width));
  event.emplace("pixel_height", JsonValue(metrics.pixel_height));
  event.emplace("source", JsonValue(metrics.source));
  Emit(JsonValue(std::move(event)));

  reader_thread_ = std::thread(&CefDaemon::ReaderLoop, this);
  CefRunMessageLoop();
  if (reader_thread_.joinable()) {
    reader_thread_.join();
  }
  return 0;
}

void CefDaemon::ReaderLoop() {
  while (!shutting_down_.load()) {
    JsonValue message;
    std::string error;
    const Ipc::ReadResult result = ipc_.Read(&message, &error);
    if (result == Ipc::ReadResult::kEnd) {
      PostToUi([this] { RequestShutdown(); });
      return;
    }
    if (result == Ipc::ReadResult::kError) {
      const bool fatal = error.empty();
      const std::string detail =
          error.empty() ? "failed to read IPC" : std::move(error);
      PostToUi([this, detail] { Error(detail); });
      if (fatal) {
        PostToUi([this] { RequestShutdown(); });
        return;
      }
      continue;
    }
    const bool shutdown = message.String("type").value_or("") == "shutdown";
    PostToUi([this, message = std::move(message)]() mutable {
      Handle(std::move(message));
    });
    if (shutdown) {
      return;
    }
  }
}

void CefDaemon::Handle(JsonValue message) {
  CEF_REQUIRE_UI_THREAD();
  const auto type = message.String("type");
  if (!type) {
    Error("message.type must be a string");
    return;
  }
  if (*type == "shutdown") {
    RequestShutdown();
    return;
  }

  std::string error;
  const std::uint32_t browser_id = BrowserId(message).value_or(0);
  if (*type == "create") {
    if (!manager_.Create(message, &error)) {
      Error(error, browser_id);
    }
    return;
  }
  if (*type == "resize") {
    if (!manager_.Resize(message, &error)) {
      Error(error, browser_id);
    }
    return;
  }
  if (*type == "attach") {
    if (!manager_.Attach(message, &error)) {
      Error(error, browser_id);
    }
    return;
  }
  if (*type == "detach") {
    if (!manager_.Detach(browser_id, &error)) {
      Error(error, browser_id);
    }
    return;
  }
  if (*type == "destroy") {
    if (!manager_.Destroy(browser_id, &error)) {
      Error(error, browser_id);
    }
    return;
  }

  CefBrowserState* state = StateFor(message, &error);
  if (!state) {
    Error(error, browser_id);
    return;
  }
  if (*type == "visibility") {
    const auto visible = message.Boolean("visible");
    if (!visible) {
      Error("visible must be a boolean", browser_id);
    } else if (!manager_.SetVisibility(browser_id, *visible, &error)) {
      Error(error, browser_id);
    }
    return;
  }
  if (*type == "focus") {
    const auto focused = message.Boolean("focused");
    if (!focused) {
      Error("focused must be a boolean", browser_id);
    } else if (!manager_.SetFocus(browser_id, *focused, &error)) {
      Error(error, browser_id);
    }
    return;
  }
  if (*type == "navigate") {
    const auto url = message.String("url");
    if (!url || url->empty()) {
      Error("url must be a non-empty string", browser_id);
    } else if (!manager_.Navigate(browser_id, *url, &error)) {
      Error(error, browser_id);
    }
    return;
  }
  if (*type == "back" || *type == "forward" || *type == "reload" ||
      *type == "stop") {
    if (!manager_.NavigationCommand(browser_id, *type, &error)) {
      Error(error, browser_id);
    }
    return;
  }
  if (*type == "scroll") {
    const auto dx = message.Integer("dx");
    const auto dy = message.Integer("dy");
    if (!dx || !dy || *dx < -1000000 || *dx > 1000000 ||
        *dy < -1000000 || *dy > 1000000) {
      Error("dx and dy must be bounded integers", browser_id);
    } else if (!manager_.Scroll(browser_id, static_cast<int>(*dx),
                                static_cast<int>(*dy), &error)) {
      Error(error, browser_id);
    }
    return;
  }
  if (*type == "scroll_to") {
    const auto edge = message.String("edge");
    if (!edge || !manager_.ScrollTo(browser_id, edge.value_or(""), &error)) {
      Error(error.empty() ? "edge must be top or bottom" : error, browser_id);
    }
    return;
  }

  if (*type == "hints_start") {
    if (!manager_.StartHints(browser_id, &error)) {
      Error(error, browser_id);
    }
    return;
  }
  if (*type == "hints_input") {
    const auto key = message.String("key");
    if (!key) {
      Error("key must be a string", browser_id);
    } else if (!manager_.HintInput(browser_id, *key, &error)) {
      Error(error, browser_id);
    }
    return;
  }
  if (*type == "hints_cancel") {
    if (!manager_.CancelHints(browser_id, &error)) {
      Error(error, browser_id);
    }
    return;
  }
  if (*type == "visual_start") {
    const auto max_hints = message.Integer("max_hints");
    if (!max_hints || *max_hints < 1 || *max_hints > 1000) {
      Error("max_hints must be in [1, 1000]", browser_id);
    } else if (!manager_.StartVisual(browser_id,
                                     static_cast<int>(*max_hints), &error)) {
      Error(error, browser_id);
    }
    return;
  }
  if (*type == "visual_hint_input") {
    const auto key = message.String("key");
    if (!key) {
      Error("key must be a string", browser_id);
    } else if (!manager_.VisualHintInput(browser_id, *key, &error)) {
      Error(error, browser_id);
    }
    return;
  }
  if (*type == "visual_move") {
    const auto operation = message.String("operation");
    if (!operation) {
      Error("operation must be a string", browser_id);
    } else if (!manager_.VisualMove(browser_id, *operation, &error)) {
      Error(error, browser_id);
    }
    return;
  }
  if (*type == "visual_yank") {
    if (!manager_.VisualYank(browser_id, &error)) {
      Error(error, browser_id);
    }
    return;
  }
  if (*type == "visual_cancel") {
    if (!manager_.CancelVisual(browser_id, &error)) {
      Error(error, browser_id);
    }
    return;
  }
  if (*type == "input_start") {
    if (!manager_.StartInput(browser_id, &error)) {
      Error(error, browser_id);
    }
    return;
  }
  if (*type == "input_text") {
    const auto text = message.String("text");
    if (!text) {
      Error("text must be a string", browser_id);
    } else if (!manager_.InputText(browser_id, *text, &error)) {
      Error(error, browser_id);
    }
    return;
  }
  if (*type == "input_key") {
    const auto key = message.String("key");
    if (!key) {
      Error("key must be a string", browser_id);
    } else if (!manager_.InputKey(
                   browser_id, *key,
                   message.Boolean("shift").value_or(false),
                   message.Boolean("control").value_or(false),
                   message.Boolean("alt").value_or(false), &error)) {
      Error(error, browser_id);
    }
    return;
  }
  if (*type == "input_cancel") {
    if (!manager_.CancelInput(browser_id, &error)) {
      Error(error, browser_id);
    }
    return;
  }

  Error("unknown message type: " + *type, browser_id);
}

void CefDaemon::RequestShutdown() {
  CEF_REQUIRE_UI_THREAD();
  if (shutting_down_.exchange(true)) {
    return;
  }
  manager_.DestroyAll();
  if (manager_.empty()) {
    CefQuitMessageLoop();
  }
}

void CefDaemon::Emit(JsonValue event) { ipc_.Write(event); }

void CefDaemon::OnBrowserCountChanged() {
  if (shutting_down_.load() && manager_.empty()) {
    CefQuitMessageLoop();
  }
}

void CefDaemon::Error(std::string message, std::uint32_t browser_id) {
  JsonValue::Object event = Event("error");
  event.emplace("message", JsonValue(std::move(message)));
  if (browser_id != 0) {
    event.emplace("browser_id",
                  JsonValue(static_cast<std::int64_t>(browser_id)));
  }
  Emit(JsonValue(std::move(event)));
}

void CefDaemon::ModeChanged(const CefBrowserState& state) {
  JsonValue::Object event = Event("mode_changed");
  event.emplace("browser_id",
                JsonValue(static_cast<std::int64_t>(state.browser_id)));
  event.emplace("mode", JsonValue(state.mode));
  Emit(JsonValue(std::move(event)));
}

CefBrowserState* CefDaemon::StateFor(const JsonValue& message,
                                     std::string* error) {
  const auto browser_id = BrowserId(message);
  if (!browser_id) {
    *error = "browser_id must be a positive uint32";
    return nullptr;
  }
  CefBrowserState* state = manager_.Find(*browser_id);
  if (!state || state->destroying) {
    *error = "unknown browser_id";
    return nullptr;
  }
  return state;
}

}  // namespace browser
