#include "app.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

namespace browser {
namespace {

JsonValue::Object Event(std::string type) {
  JsonValue::Object event;
  event.emplace("type", JsonValue(std::move(type)));
  return event;
}

std::optional<std::uint32_t> BrowserId(const JsonValue& message) {
  const auto value = message.Integer("browser_id");
  if (!value || *value < 1 ||
      *value > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(*value);
}

}  // namespace

App::App(bool dry_run, std::istream& input, std::ostream& output)
    : renderer_(dry_run), manager_(&renderer_), ipc_(input, output) {}

void App::Error(std::string message, std::uint32_t browser_id) {
  JsonValue::Object event = Event("error");
  event.emplace("message", JsonValue(std::move(message)));
  if (browser_id != 0) {
    event.emplace("browser_id", JsonValue(static_cast<std::int64_t>(browser_id)));
  }
  ipc_.Write(JsonValue(std::move(event)));
}

void App::ModeChanged(const BrowserState& state) {
  JsonValue::Object event = Event("mode_changed");
  event.emplace("browser_id",
                JsonValue(static_cast<std::int64_t>(state.browser_id)));
  event.emplace("mode", JsonValue(state.mode));
  ipc_.Write(JsonValue(std::move(event)));
}

BrowserState* App::StateFor(const JsonValue& message, std::string* error) {
  const auto browser_id = BrowserId(message);
  if (!browser_id) {
    *error = "browser_id must be a positive uint32";
    return nullptr;
  }
  BrowserState* state = manager_.Find(*browser_id);
  if (!state) {
    *error = "unknown browser_id";
  }
  return state;
}

bool App::Handle(const JsonValue& message) {
  const auto type = message.String("type");
  if (!type) {
    Error("message.type must be a string");
    return true;
  }
  if (*type == "shutdown") {
    manager_.DestroyAll();
    shutting_down_ = true;
    return false;
  }

  std::string error;
  const std::uint32_t event_browser_id = BrowserId(message).value_or(0);
  if (*type == "create") {
    if (!manager_.Create(message, &error)) {
      Error(error, event_browser_id);
      return true;
    }
    const BrowserState* state = manager_.Find(event_browser_id);
    JsonValue::Object event = Event("created");
    event.emplace("browser_id",
                  JsonValue(static_cast<std::int64_t>(event_browser_id)));
    ipc_.Write(JsonValue(std::move(event)));
    if (state) {
      JsonValue::Object changed = Event("url_changed");
      changed.emplace("browser_id",
                      JsonValue(static_cast<std::int64_t>(event_browser_id)));
      changed.emplace("url", JsonValue(state->url));
      ipc_.Write(JsonValue(std::move(changed)));
    }
    return true;
  }
  if (*type == "resize") {
    if (!manager_.Resize(message, &error)) {
      Error(error, event_browser_id);
    }
    return true;
  }
  if (*type == "attach") {
    if (!manager_.Attach(message, &error)) {
      Error(error, event_browser_id);
    }
    return true;
  }
  if (*type == "detach") {
    if (!manager_.Detach(event_browser_id, &error)) {
      Error(error, event_browser_id);
    }
    return true;
  }
  if (*type == "destroy") {
    if (!manager_.Destroy(event_browser_id, &error)) {
      Error(error, event_browser_id);
    }
    return true;
  }

  BrowserState* state = StateFor(message, &error);
  if (!state) {
    Error(error, event_browser_id);
    return true;
  }
  if (*type == "visibility") {
    const auto visible = message.Boolean("visible");
    if (!visible) {
      Error("visible must be a boolean", state->browser_id);
    } else {
      state->visible = *visible;
    }
    return true;
  }
  if (*type == "focus") {
    const auto focused = message.Boolean("focused");
    if (!focused) {
      Error("focused must be a boolean", state->browser_id);
    } else {
      state->focused = *focused;
    }
    return true;
  }
  if (*type == "navigate") {
    const auto url = message.String("url");
    if (!url || url->empty()) {
      Error("url must be a non-empty string", state->browser_id);
      return true;
    }
    state->url = *url;
    JsonValue::Object event = Event("url_changed");
    event.emplace("browser_id",
                  JsonValue(static_cast<std::int64_t>(state->browser_id)));
    event.emplace("url", JsonValue(state->url));
    ipc_.Write(JsonValue(std::move(event)));
    return true;
  }

  if (*type == "hints_start") {
    if (state->mode != "normal") {
      Error("hints_start is only valid in normal mode", state->browser_id);
    } else {
      state->mode = "hint";
      ModeChanged(*state);
    }
    return true;
  }
  if (*type == "hints_input") {
    if (state->mode != "hint") {
      Error("hints_input is only valid in hint mode", state->browser_id);
    } else {
      state->mode = "normal";
      ModeChanged(*state);
    }
    return true;
  }
  if (*type == "hints_cancel") {
    if (state->mode == "hint") {
      state->mode = "normal";
      ModeChanged(*state);
    }
    return true;
  }
  if (*type == "cursor_move") {
    if (state->mode != "normal") {
      Error("cursor_move is only valid in normal mode", state->browser_id);
    }
    return true;
  }
  if (*type == "cursor_activate") {
    if (state->mode != "normal") {
      Error("cursor_activate is only valid in normal mode", state->browser_id);
    }
    return true;
  }
  if (*type == "visual_cursor_start") {
    if (state->mode != "normal") {
      Error("visual_cursor_start is only valid in normal mode",
            state->browser_id);
    } else {
      state->mode = "visual";
      ModeChanged(*state);
    }
    return true;
  }
  if (*type == "visual_start") {
    if (state->mode != "normal") {
      Error("visual_start is only valid in normal mode", state->browser_id);
    } else {
      state->mode = "visual_hint";
      ModeChanged(*state);
    }
    return true;
  }
  if (*type == "visual_hint_input") {
    if (state->mode != "visual_hint") {
      Error("visual_hint_input is only valid in visual hint mode",
            state->browser_id);
    } else {
      state->mode = "visual";
      ModeChanged(*state);
    }
    return true;
  }
  if (*type == "visual_move") {
    if (state->mode != "visual") {
      Error("visual_move is only valid in visual mode", state->browser_id);
    }
    return true;
  }
  if (*type == "visual_yank") {
    if (state->mode != "visual") {
      Error("visual_yank is only valid in visual mode", state->browser_id);
    } else {
      Error("visual selection requires the Phase 2 CEF renderer",
            state->browser_id);
    }
    return true;
  }
  if (*type == "visual_cancel") {
    if (state->mode == "visual" || state->mode == "visual_hint") {
      state->mode = "normal";
      ModeChanged(*state);
    }
    return true;
  }
  if (*type == "input_cursor_start") {
    if (state->mode != "normal") {
      Error("input_cursor_start is only valid in normal mode",
            state->browser_id);
    }
    return true;
  }
  if (*type == "input_start") {
    if (state->mode != "normal") {
      Error("input_start is only valid in normal mode", state->browser_id);
    } else {
      state->mode = "insert";
      ModeChanged(*state);
    }
    return true;
  }
  if (*type == "input_text" || *type == "input_key") {
    if (state->mode != "insert") {
      Error(*type + " is only valid in insert mode", state->browser_id);
    }
    return true;
  }
  if (*type == "input_cancel") {
    if (state->mode != "insert") {
      Error("input_cancel is only valid in insert mode", state->browser_id);
    } else {
      state->mode = "normal";
      ModeChanged(*state);
    }
    return true;
  }

  if (*type == "back" || *type == "forward" || *type == "reload" ||
      *type == "stop" || *type == "scroll" || *type == "scroll_to") {
    return true;
  }

  Error("unknown message type: " + *type, state->browser_id);
  return true;
}

int App::Run() {
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
  ipc_.Write(JsonValue(std::move(event)));

  while (!shutting_down_) {
    JsonValue message;
    std::string error;
    const Ipc::ReadResult result = ipc_.Read(&message, &error);
    if (result == Ipc::ReadResult::kEnd) {
      break;
    }
    if (result == Ipc::ReadResult::kError) {
      Error(error.empty() ? "failed to read IPC" : error);
      if (error.empty()) {
        return 1;
      }
      continue;
    }
    if (!Handle(message)) {
      break;
    }
  }
  manager_.DestroyAll();
  return 0;
}

}  // namespace browser
