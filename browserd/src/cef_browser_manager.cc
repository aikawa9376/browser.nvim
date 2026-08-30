#include "cef_browser_manager.h"

#include "cef_browser_client.h"
#include "include/wrapper/cef_helpers.h"

#include <limits>
#include <sstream>
#include <utility>
#include <vector>

namespace browser {
namespace {

JsonValue::Object Event(std::string type, std::uint32_t browser_id) {
  JsonValue::Object event;
  event.emplace("type", JsonValue(std::move(type)));
  event.emplace("browser_id",
                JsonValue(static_cast<std::int64_t>(browser_id)));
  return event;
}

bool IsCaretOperation(std::string_view operation) {
  constexpr std::string_view kOperations[] = {
      "previous_grapheme", "next_grapheme", "previous_word", "next_word",
      "up",                "down",          "line_start",    "line_end",
  };
  for (const std::string_view candidate : kOperations) {
    if (operation == candidate) {
      return true;
    }
  }
  return false;
}

bool PrepareDirtyRects(const CefBrowserState& state,
                       const CefRenderHandler::RectList& dirty_rects,
                       int width,
                       int height,
                       std::vector<PixelRect>* merged) {
  if (!state.dirty_rects || dirty_rects.empty() ||
      dirty_rects.size() >
          static_cast<std::size_t>(state.max_dirty_rects)) {
    return false;
  }
  std::vector<PixelRect> clipped;
  clipped.reserve(dirty_rects.size());
  for (const CefRect& dirty : dirty_rects) {
    const PixelRect rect =
        ClipPixelRect({dirty.x, dirty.y, dirty.width, dirty.height}, width,
                      height);
    if (rect.width > 0 && rect.height > 0) {
      clipped.push_back(rect);
    }
  }
  *merged = MergePixelRects(clipped);
  if (merged->empty() ||
      merged->size() > static_cast<std::size_t>(state.max_dirty_rects)) {
    return false;
  }
  std::uint64_t area = 0;
  for (const PixelRect rect : *merged) {
    area += static_cast<std::uint64_t>(rect.width) *
            static_cast<std::uint64_t>(rect.height);
  }
  const double viewport_area = static_cast<double>(width) * height;
  return static_cast<double>(area) <=
         viewport_area * state.full_frame_threshold;
}

}  // namespace

CefBrowserManager::~CefBrowserManager() {
  for (auto& [browser_id, state] : states_) {
    (void)browser_id;
    std::string ignored;
    CleanupKitty(&state, &ignored);
  }
}

bool CefBrowserManager::ReadPositive(const JsonValue& message,
                                     std::string_view key,
                                     std::uint32_t* output,
                                     std::string* error) {
  const auto value = message.Integer(key);
  if (!value || *value < 1 ||
      *value > static_cast<std::int64_t>(
                   std::numeric_limits<std::uint32_t>::max())) {
    if (error) {
      *error = std::string(key) + " must be a positive uint32";
    }
    return false;
  }
  *output = static_cast<std::uint32_t>(*value);
  return true;
}

bool CefBrowserManager::ReadDimension(const JsonValue& message,
                                      std::string_view key,
                                      int* output,
                                      std::string* error) {
  const auto value = message.Integer(key);
  if (!value || *value < 1 || *value > 16384) {
    if (error) {
      *error = std::string(key) + " must be in [1, 16384]";
    }
    return false;
  }
  *output = static_cast<int>(*value);
  return true;
}

bool CefBrowserManager::Create(const JsonValue& message, std::string* error) {
  CEF_REQUIRE_UI_THREAD();
  CefBrowserState state;
  if (!ReadPositive(message, "browser_id", &state.browser_id, error) ||
      !ReadPositive(message, "anchor_image_id", &state.anchor_image_id,
                    error) ||
      !ReadPositive(message, "anchor_placement_id",
                    &state.anchor_placement_id, error) ||
      !ReadPositive(message, "browser_image_id", &state.browser_image_id,
                    error) ||
      !ReadPositive(message, "browser_placement_id",
                    &state.browser_placement_id, error) ||
      !ReadDimension(message, "cols", &state.columns, error) ||
      !ReadDimension(message, "rows", &state.rows, error) ||
      !ReadDimension(message, "width", &state.width, error) ||
      !ReadDimension(message, "height", &state.height, error)) {
    return false;
  }
  const auto url = message.String("url");
  if (!url || url->empty()) {
    if (error) {
      *error = "url must be a non-empty string";
    }
    return false;
  }
  state.url = *url;
  if (const auto fps = message.Integer("fps")) {
    if (*fps < 1 || *fps > 240) {
      if (error) {
        *error = "fps must be in [1, 240]";
      }
      return false;
    }
    state.fps = static_cast<int>(*fps);
  }
  if (const JsonValue* dirty_rects = message.Find("dirty_rects")) {
    if (!dirty_rects->is_bool()) {
      if (error) {
        *error = "dirty_rects must be a boolean";
      }
      return false;
    }
    state.dirty_rects = dirty_rects->boolean();
  }
  if (const auto maximum = message.Integer("max_dirty_rects")) {
    if (*maximum < 1 || *maximum > 1024) {
      if (error) {
        *error = "max_dirty_rects must be in [1, 1024]";
      }
      return false;
    }
    state.max_dirty_rects = static_cast<int>(*maximum);
  }
  if (const JsonValue* threshold = message.Find("full_frame_threshold")) {
    if (!threshold->is_number() || threshold->number() <= 0.0 ||
        threshold->number() > 1.0) {
      if (error) {
        *error = "full_frame_threshold must be in (0, 1]";
      }
      return false;
    }
    state.full_frame_threshold = threshold->number();
  }
  if (states_.contains(state.browser_id)) {
    if (error) {
      *error = "browser_id already exists";
    }
    return false;
  }

  if (!renderer_->UploadAnchor(state.browser_id, state.anchor_image_id,
                               state.anchor_placement_id, error)) {
    return false;
  }
  const std::uint32_t browser_id = state.browser_id;
  state.client = new CefBrowserClient(browser_id, this);
  auto [entry, inserted] = states_.emplace(browser_id, std::move(state));
  if (!inserted) {
    if (error) {
      *error = "browser_id already exists";
    }
    return false;
  }

  CefWindowInfo window_info;
  window_info.SetAsWindowless(0);
  CefBrowserSettings settings;
  settings.windowless_frame_rate = entry->second.fps;
  settings.background_color = CefColorSetARGB(255, 255, 255, 255);
  const bool started = CefBrowserHost::CreateBrowser(
      window_info, entry->second.client, entry->second.url, settings, nullptr,
      CefRequestContext::GetGlobalContext());
  if (!started) {
    std::string ignored;
    CleanupKitty(&entry->second, &ignored);
    states_.erase(entry);
    if (error) {
      *error = "CEF rejected the browser creation request";
    }
    return false;
  }
  return true;
}

bool CefBrowserManager::Resize(const JsonValue& message, std::string* error) {
  CEF_REQUIRE_UI_THREAD();
  std::uint32_t browser_id = 0;
  int columns = 0;
  int rows = 0;
  int width = 0;
  int height = 0;
  if (!ReadPositive(message, "browser_id", &browser_id, error) ||
      !ReadDimension(message, "cols", &columns, error) ||
      !ReadDimension(message, "rows", &rows, error) ||
      !ReadDimension(message, "width", &width, error) ||
      !ReadDimension(message, "height", &height, error)) {
    return false;
  }
  CefBrowserState* state = Find(browser_id);
  if (!state || state->destroying) {
    if (error) {
      *error = "unknown browser_id";
    }
    return false;
  }
  const bool pixels_changed = state->width != width || state->height != height;
  const bool cells_changed =
      state->columns != columns || state->rows != rows;
  state->columns = columns;
  state->rows = rows;
  state->width = width;
  state->height = height;
  if (state->browser && pixels_changed) {
    state->browser->GetHost()->WasResized();
  }
  if (state->attached && cells_changed && state->framebuffer.has_view()) {
    return renderer_->PlaceRelative(
        state->browser_image_id, state->browser_placement_id,
        state->anchor_image_id, state->anchor_placement_id, state->columns,
        state->rows, error);
  }
  return true;
}

bool CefBrowserManager::Attach(const JsonValue& message, std::string* error) {
  CEF_REQUIRE_UI_THREAD();
  std::uint32_t browser_id = 0;
  if (!ReadPositive(message, "browser_id", &browser_id, error)) {
    return false;
  }
  CefBrowserState* state = Find(browser_id);
  if (!state || state->destroying) {
    if (error) {
      *error = "unknown browser_id";
    }
    return false;
  }
  if (const auto columns = message.Integer("cols")) {
    if (*columns < 1 || *columns > 16384) {
      if (error) {
        *error = "cols must be in [1, 16384]";
      }
      return false;
    }
    state->columns = static_cast<int>(*columns);
  }
  if (const auto rows = message.Integer("rows")) {
    if (*rows < 1 || *rows > 16384) {
      if (error) {
        *error = "rows must be in [1, 16384]";
      }
      return false;
    }
    state->rows = static_cast<int>(*rows);
  }

  state->attached = true;
  if (message.Boolean("full_frame").value_or(false) &&
      !renderer_->UploadAnchor(state->browser_id, state->anchor_image_id,
                               state->anchor_placement_id, error)) {
    state->attached = false;
    return false;
  }
  if (state->framebuffer.has_view()) {
    return Render(state, error);
  }
  if (state->browser) {
    state->browser->GetHost()->Invalidate(PET_VIEW);
  }
  return true;
}

bool CefBrowserManager::Detach(std::uint32_t browser_id, std::string* error) {
  CEF_REQUIRE_UI_THREAD();
  CefBrowserState* state = Find(browser_id);
  if (!state || state->destroying) {
    if (error) {
      *error = "unknown browser_id";
    }
    return false;
  }
  if (state->attached &&
      !renderer_->DeletePlacement(state->browser_image_id,
                                  state->browser_placement_id, error)) {
    return false;
  }
  state->attached = false;
  return true;
}

bool CefBrowserManager::Destroy(std::uint32_t browser_id,
                                std::string* error) {
  CEF_REQUIRE_UI_THREAD();
  CefBrowserState* state = Find(browser_id);
  if (!state) {
    return true;
  }
  if (state->destroying) {
    return true;
  }
  state->destroying = true;
  CleanupKitty(state, error);
  if (state->browser) {
    state->browser->GetHost()->CloseBrowser(true);
  }
  return true;
}

void CefBrowserManager::DestroyAll() {
  CEF_REQUIRE_UI_THREAD();
  // CloseBrowser() may synchronously invoke OnBeforeClose() for a windowless
  // browser, which erases the map entry. Iterate over stable IDs rather than
  // holding map iterators across the close call.
  std::vector<std::uint32_t> browser_ids;
  browser_ids.reserve(states_.size());
  for (const auto& [browser_id, state] : states_) {
    if (!state.destroying) {
      browser_ids.push_back(browser_id);
    }
  }
  for (const std::uint32_t browser_id : browser_ids) {
    CefBrowserState* state = Find(browser_id);
    if (state && !state->destroying) {
      state->destroying = true;
      std::string ignored;
      CleanupKitty(state, &ignored);
      if (state->browser) {
        CefRefPtr<CefBrowser> browser = state->browser;
        browser->GetHost()->CloseBrowser(true);
      }
    }
  }
}

bool CefBrowserManager::SetVisibility(std::uint32_t browser_id,
                                      bool visible,
                                      std::string* error) {
  CefBrowserState* state = Find(browser_id);
  if (!state || state->destroying) {
    if (error) {
      *error = "unknown browser_id";
    }
    return false;
  }
  state->visible = visible;
  if (state->browser) {
    state->browser->GetHost()->WasHidden(!visible);
    if (visible) {
      state->browser->GetHost()->Invalidate(PET_VIEW);
    }
  }
  return true;
}

bool CefBrowserManager::SetFocus(std::uint32_t browser_id,
                                 bool focused,
                                 std::string* error) {
  CefBrowserState* state = Find(browser_id);
  if (!state || state->destroying) {
    if (error) {
      *error = "unknown browser_id";
    }
    return false;
  }
  state->focused = focused;
  if (state->browser) {
    state->browser->GetHost()->SetFocus(focused);
  }
  return true;
}

bool CefBrowserManager::Navigate(std::uint32_t browser_id,
                                 std::string url,
                                 std::string* error) {
  CefBrowserState* state = Find(browser_id);
  if (!state || state->destroying) {
    if (error) {
      *error = "unknown browser_id";
    }
    return false;
  }
  state->url = std::move(url);
  if (state->mode != "normal") {
    SetMode(state, "normal");
  }
  if (state->browser) {
    state->page_ready = false;
    state->browser->GetMainFrame()->LoadURL(state->url);
  }
  return true;
}

bool CefBrowserManager::NavigationCommand(std::uint32_t browser_id,
                                          std::string_view command,
                                          std::string* error) {
  CefBrowserState* state = Find(browser_id);
  if (!state || state->destroying) {
    if (error) {
      *error = "unknown browser_id";
    }
    return false;
  }
  if (command != "back" && command != "forward" && command != "reload" &&
      command != "stop") {
    if (error) {
      *error = "unknown navigation command";
    }
    return false;
  }
  if (!state->browser || (!state->page_ready && command != "stop")) {
    state->pending_navigation_commands.emplace_back(command);
    return true;
  }
  ExecuteNavigationCommand(state, command);
  return true;
}

bool CefBrowserManager::Scroll(std::uint32_t browser_id,
                               int dx,
                               int dy,
                               std::string* error) {
  CefBrowserState* state = Find(browser_id);
  if (!state || state->destroying) {
    if (error) {
      *error = "unknown browser_id";
    }
    return false;
  }
  const std::string script = "window.scrollBy(" + std::to_string(dx) + "," +
                             std::to_string(dy) + ");";
  ExecuteScript(state, script);
  return true;
}

bool CefBrowserManager::ScrollTo(std::uint32_t browser_id,
                                 std::string_view edge,
                                 std::string* error) {
  CefBrowserState* state = Find(browser_id);
  if (!state || state->destroying) {
    if (error) {
      *error = "unknown browser_id";
    }
    return false;
  }
  std::string script;
  if (edge == "top") {
    script = "window.scrollTo(0,0);";
  } else if (edge == "bottom") {
    script =
        "window.scrollTo(0,Math.max(document.documentElement.scrollHeight,"
        "document.body?document.body.scrollHeight:0));";
  } else {
    if (error) {
      *error = "edge must be top or bottom";
    }
    return false;
  }
  ExecuteScript(state, std::move(script));
  return true;
}

bool CefBrowserManager::StartHints(std::uint32_t browser_id,
                                   std::string* error) {
  CEF_REQUIRE_UI_THREAD();
  CefBrowserState* state = Find(browser_id);
  if (!state || state->destroying) {
    if (error) {
      *error = "unknown browser_id";
    }
    return false;
  }
  if (!state->browser || !state->page_ready) {
    if (error) {
      *error = "page is not ready for link hints";
    }
    return false;
  }
  if (state->mode != "normal") {
    if (error) {
      *error = "hints_start is only valid in normal mode";
    }
    return false;
  }
  SetMode(state, "hint");
  ExecuteScript(
      state,
      "window.__nvimBrowser&&window.__nvimBrowser.hints&&"
      "window.__nvimBrowser.hints.start();");
  return true;
}

bool CefBrowserManager::HintInput(std::uint32_t browser_id,
                                  std::string_view key,
                                  std::string* error) {
  CEF_REQUIRE_UI_THREAD();
  CefBrowserState* state = Find(browser_id);
  if (!state || state->destroying) {
    if (error) {
      *error = "unknown browser_id";
    }
    return false;
  }
  if (state->mode != "hint") {
    if (error) {
      *error = "hints_input is only valid in hint mode";
    }
    return false;
  }
  constexpr std::string_view kAlphabet = "asdfghjkl";
  if (key.size() != 1 || kAlphabet.find(key.front()) == std::string::npos) {
    if (error) {
      *error = "hint key must be one of asdfghjkl";
    }
    return false;
  }
  const std::string script =
      "window.__nvimBrowser.hints.input(\"" + std::string(key) + "\");";
  ExecuteScript(state, script);
  return true;
}

bool CefBrowserManager::CancelHints(std::uint32_t browser_id,
                                    std::string* error) {
  CEF_REQUIRE_UI_THREAD();
  CefBrowserState* state = Find(browser_id);
  if (!state || state->destroying) {
    if (error) {
      *error = "unknown browser_id";
    }
    return false;
  }
  if (state->mode != "hint") {
    if (error) {
      *error = "hints_cancel is only valid in hint mode";
    }
    return false;
  }
  ExecuteScript(
      state,
      "window.__nvimBrowser&&window.__nvimBrowser.hints&&"
      "window.__nvimBrowser.hints.cancel();");
  SetMode(state, "normal");
  return true;
}

bool CefBrowserManager::NormalMove(std::uint32_t browser_id,
                                   std::string_view operation,
                                   std::string* error) {
  CEF_REQUIRE_UI_THREAD();
  CefBrowserState* state = Find(browser_id);
  if (!state || state->destroying) {
    if (error) {
      *error = "unknown browser_id";
    }
    return false;
  }
  if (!state->browser || !state->page_ready || state->mode != "normal") {
    if (error) {
      *error = "cursor_move requires a ready page in normal mode";
    }
    return false;
  }
  if (!IsCaretOperation(operation)) {
    if (error) {
      *error = "unsupported cursor movement";
    }
    return false;
  }
  ExecuteScript(state, "window.__nvimBrowser.visual.normalMove(\"" +
                           std::string(operation) + "\");");
  return true;
}

bool CefBrowserManager::StartVisualAtCursor(std::uint32_t browser_id,
                                            std::string* error) {
  CEF_REQUIRE_UI_THREAD();
  CefBrowserState* state = Find(browser_id);
  if (!state || state->destroying) {
    if (error) {
      *error = "unknown browser_id";
    }
    return false;
  }
  if (!state->browser || !state->page_ready || state->mode != "normal") {
    if (error) {
      *error = "visual_cursor_start requires a ready page in normal mode";
    }
    return false;
  }
  SetMode(state, "visual");
  ExecuteScript(state,
                "window.__nvimBrowser.visual.startAtCursor();");
  return true;
}

bool CefBrowserManager::StartVisual(std::uint32_t browser_id,
                                    int max_hints,
                                    std::string* error) {
  CEF_REQUIRE_UI_THREAD();
  CefBrowserState* state = Find(browser_id);
  if (!state || state->destroying) {
    if (error) {
      *error = "unknown browser_id";
    }
    return false;
  }
  if (!state->browser || !state->page_ready) {
    if (error) {
      *error = "page is not ready for visual selection";
    }
    return false;
  }
  if (state->mode != "normal") {
    if (error) {
      *error = "visual_start is only valid in normal mode";
    }
    return false;
  }
  if (max_hints < 1 || max_hints > 1000) {
    if (error) {
      *error = "max_hints must be in [1, 1000]";
    }
    return false;
  }
  state->max_visual_hints = max_hints;
  SetMode(state, "visual_hint");
  ExecuteScript(
      state,
      "window.__nvimBrowser&&window.__nvimBrowser.visual&&"
      "window.__nvimBrowser.visual.start(" + std::to_string(max_hints) +
          ");");
  return true;
}

bool CefBrowserManager::VisualHintInput(std::uint32_t browser_id,
                                        std::string_view key,
                                        std::string* error) {
  CEF_REQUIRE_UI_THREAD();
  CefBrowserState* state = Find(browser_id);
  if (!state || state->destroying) {
    if (error) {
      *error = "unknown browser_id";
    }
    return false;
  }
  if (state->mode != "visual_hint") {
    if (error) {
      *error = "visual_hint_input is only valid in visual_hint mode";
    }
    return false;
  }
  constexpr std::string_view kAlphabet = "asdfghjkl";
  if (key.size() != 1 || kAlphabet.find(key.front()) == std::string::npos) {
    if (error) {
      *error = "visual hint key must be one of asdfghjkl";
    }
    return false;
  }
  ExecuteScript(state, "window.__nvimBrowser.visual.hintInput(\"" +
                           std::string(key) + "\");");
  return true;
}

bool CefBrowserManager::VisualMove(std::uint32_t browser_id,
                                   std::string_view operation,
                                   std::string* error) {
  CEF_REQUIRE_UI_THREAD();
  CefBrowserState* state = Find(browser_id);
  if (!state || state->destroying) {
    if (error) {
      *error = "unknown browser_id";
    }
    return false;
  }
  if (state->mode != "visual") {
    if (error) {
      *error = "visual_move is only valid in visual mode";
    }
    return false;
  }
  if (!IsCaretOperation(operation) && operation != "swap") {
    if (error) {
      *error = "unsupported visual movement";
    }
    return false;
  }
  ExecuteScript(state, "window.__nvimBrowser.visual.move(\"" +
                           std::string(operation) + "\");");
  return true;
}

bool CefBrowserManager::VisualYank(std::uint32_t browser_id,
                                   std::string* error) {
  CEF_REQUIRE_UI_THREAD();
  CefBrowserState* state = Find(browser_id);
  if (!state || state->destroying) {
    if (error) {
      *error = "unknown browser_id";
    }
    return false;
  }
  if (state->mode != "visual") {
    if (error) {
      *error = "visual_yank is only valid in visual mode";
    }
    return false;
  }
  ExecuteScript(state, "window.__nvimBrowser.visual.yank();");
  return true;
}

bool CefBrowserManager::CancelVisual(std::uint32_t browser_id,
                                     std::string* error) {
  CEF_REQUIRE_UI_THREAD();
  CefBrowserState* state = Find(browser_id);
  if (!state || state->destroying) {
    if (error) {
      *error = "unknown browser_id";
    }
    return false;
  }
  if (state->mode != "visual" && state->mode != "visual_hint") {
    if (error) {
      *error = "visual_cancel is only valid in a visual mode";
    }
    return false;
  }
  ExecuteScript(state, "window.__nvimBrowser.visual.cancel();");
  SetMode(state, "normal");
  return true;
}

bool CefBrowserManager::StartInputAtCursor(std::uint32_t browser_id,
                                           std::string* error) {
  CEF_REQUIRE_UI_THREAD();
  CefBrowserState* state = Find(browser_id);
  if (!state || state->destroying) {
    if (error) {
      *error = "unknown browser_id";
    }
    return false;
  }
  if (!state->browser || !state->page_ready || state->mode != "normal") {
    if (error) {
      *error = "input_cursor_start requires a ready page in normal mode";
    }
    return false;
  }
  ExecuteScript(state,
                "window.__nvimBrowser.visual.focusCursorEditable();");
  return true;
}

bool CefBrowserManager::StartInput(std::uint32_t browser_id,
                                   std::string* error) {
  CEF_REQUIRE_UI_THREAD();
  CefBrowserState* state = Find(browser_id);
  if (!state || state->destroying) {
    if (error) {
      *error = "unknown browser_id";
    }
    return false;
  }
  if (!state->browser || !state->page_ready || state->mode != "normal") {
    if (error) {
      *error = "input_start requires a ready page in normal mode";
    }
    return false;
  }
  SetMode(state, "insert");
  return true;
}

bool CefBrowserManager::InputText(std::uint32_t browser_id,
                                  std::string_view text,
                                  std::string* error) {
  CEF_REQUIRE_UI_THREAD();
  CefBrowserState* state = Find(browser_id);
  if (!state || !state->browser || state->destroying ||
      state->mode != "insert") {
    if (error) {
      *error = "text input is only valid in insert mode";
    }
    return false;
  }
  if (text.empty() || text.size() > 16384) {
    if (error) {
      *error = "input text must contain between 1 and 16384 bytes";
    }
    return false;
  }
  const CefString utf16{std::string(text)};
  for (std::size_t index = 0; index < utf16.length(); ++index) {
    CefKeyEvent event;
    event.type = KEYEVENT_CHAR;
    event.windows_key_code = utf16.c_str()[index];
    event.native_key_code = utf16.c_str()[index];
    event.character = utf16.c_str()[index];
    event.unmodified_character = utf16.c_str()[index];
    event.focus_on_editable_field = true;
    state->browser->GetHost()->SendKeyEvent(event);
  }
  return true;
}

bool CefBrowserManager::InputKey(std::uint32_t browser_id,
                                 std::string_view key,
                                 bool shift,
                                 bool control,
                                 bool alt,
                                 std::string* error) {
  CEF_REQUIRE_UI_THREAD();
  CefBrowserState* state = Find(browser_id);
  if (!state || !state->browser || state->destroying ||
      state->mode != "insert") {
    if (error) {
      *error = "key input is only valid in insert mode";
    }
    return false;
  }
  int key_code = 0;
  if (key == "Backspace") {
    key_code = 0x08;
  } else if (key == "Tab") {
    key_code = 0x09;
  } else if (key == "Enter") {
    key_code = 0x0d;
  } else if (key == "Home") {
    key_code = 0x24;
  } else if (key == "Left") {
    key_code = 0x25;
  } else if (key == "Up") {
    key_code = 0x26;
  } else if (key == "Right") {
    key_code = 0x27;
  } else if (key == "Down") {
    key_code = 0x28;
  } else if (key == "End") {
    key_code = 0x23;
  } else if (key == "Delete") {
    key_code = 0x2e;
  } else if (key.size() == 1 && key.front() >= 'a' && key.front() <= 'z') {
    key_code = static_cast<int>(key.front() - 'a' + 'A');
  } else {
    if (error) {
      *error = "unsupported input key";
    }
    return false;
  }

  std::uint32_t modifiers = EVENTFLAG_NONE;
  if (shift) {
    modifiers |= EVENTFLAG_SHIFT_DOWN;
  }
  if (control) {
    modifiers |= EVENTFLAG_CONTROL_DOWN;
  }
  if (alt) {
    modifiers |= EVENTFLAG_ALT_DOWN;
  }
  CefKeyEvent event;
  event.type = KEYEVENT_RAWKEYDOWN;
  event.modifiers = modifiers;
  event.windows_key_code = key_code;
  event.native_key_code = key_code;
  event.focus_on_editable_field = true;
  state->browser->GetHost()->SendKeyEvent(event);
  if (key == "Enter") {
    event.type = KEYEVENT_CHAR;
    event.character = '\r';
    event.unmodified_character = '\r';
    state->browser->GetHost()->SendKeyEvent(event);
  }
  event.type = KEYEVENT_KEYUP;
  state->browser->GetHost()->SendKeyEvent(event);
  return true;
}

bool CefBrowserManager::CancelInput(std::uint32_t browser_id,
                                    std::string* error) {
  CEF_REQUIRE_UI_THREAD();
  CefBrowserState* state = Find(browser_id);
  if (!state || state->destroying) {
    if (error) {
      *error = "unknown browser_id";
    }
    return false;
  }
  if (state->mode != "insert") {
    if (error) {
      *error = "input_cancel is only valid in insert mode";
    }
    return false;
  }
  ExecuteScript(
      state,
      "document.activeElement&&document.activeElement.blur&&"
      "document.activeElement.blur();");
  SetMode(state, "normal");
  return true;
}

bool CefBrowserManager::HandleBridgeQuery(std::uint32_t browser_id,
                                          CefRefPtr<CefBrowser> browser,
                                          CefRefPtr<CefFrame> frame,
                                          std::string_view request,
                                          std::string* response,
                                          std::string* error) {
  CEF_REQUIRE_UI_THREAD();
  CefBrowserState* state = Find(browser_id);
  if (!state || state->destroying || !state->browser ||
      !state->browser->IsSame(browser)) {
    *error = "bridge browser identity mismatch";
    return false;
  }
  if (!frame || !frame->IsMain() || frame->GetURL().ToString() != state->url) {
    *error = "bridge frame is not the active main frame";
    return false;
  }
  std::string parse_error;
  const auto message = JsonValue::Parse(request, &parse_error);
  if (!message || !message->is_object()) {
    *error = "invalid bridge JSON: " + parse_error;
    return false;
  }
  const auto kind = message->String("kind");
  if (!kind) {
    *error = "bridge kind must be a string";
    return false;
  }
  if (*kind == "hints_started") {
    if (state->mode != "hint") {
      *error = "hint bridge event received outside hint mode";
      return false;
    }
    const auto count = message->Integer("count");
    if (!count || *count < 1 || *count > 10000) {
      *error = "hint count must be in [1, 10000]";
      return false;
    }
    JsonValue::Object event = Event("hints_ready", browser_id);
    event.emplace("count", JsonValue(*count));
    sink_->Emit(JsonValue(std::move(event)));
  } else if (*kind == "hint_activated") {
    if (state->mode != "hint") {
      *error = "hint bridge event received outside hint mode";
      return false;
    }
    const std::string action = message->String("action").value_or("");
    const std::string tag = message->String("tag").value_or("");
    if ((action != "click" && action != "focus") || tag.size() > 32) {
      *error = "invalid hint activation result";
      return false;
    }
    SetMode(state, action == "focus" ? "insert" : "normal");
    JsonValue::Object event = Event("hint_activated", browser_id);
    event.emplace("action", JsonValue(action));
    event.emplace("tag", JsonValue(tag));
    sink_->Emit(JsonValue(std::move(event)));
  } else if (*kind == "hints_cancelled") {
    if (state->mode != "hint") {
      *error = "hint bridge event received outside hint mode";
      return false;
    }
    SetMode(state, "normal");
    JsonValue::Object event = Event("hints_cancelled", browser_id);
    event.emplace("reason",
                  JsonValue(message->String("reason").value_or("cancelled")));
    sink_->Emit(JsonValue(std::move(event)));
  } else if (*kind == "hints_empty") {
    if (state->mode != "hint") {
      *error = "hint bridge event received outside hint mode";
      return false;
    }
    SetMode(state, "normal");
    JsonValue::Object event = Event("hints_empty", browser_id);
    sink_->Emit(JsonValue(std::move(event)));
  } else if (*kind == "visual_hints_started") {
    if (state->mode != "visual_hint") {
      *error = "visual hint event received outside visual_hint mode";
      return false;
    }
    const auto count = message->Integer("count");
    if (!count || *count < 1 || *count > state->max_visual_hints) {
      *error = "visual hint count exceeds the configured limit";
      return false;
    }
    JsonValue::Object event = Event("visual_hints_ready", browser_id);
    event.emplace("count", JsonValue(*count));
    sink_->Emit(JsonValue(std::move(event)));
  } else if (*kind == "visual_started") {
    if (state->mode != "visual_hint" && state->mode != "visual") {
      *error = "visual_started received outside a visual start";
      return false;
    }
    if (state->mode == "visual_hint") {
      SetMode(state, "visual");
    }
  } else if (*kind == "visual_cancelled" || *kind == "visual_empty") {
    if (state->mode != "visual_hint" && state->mode != "visual") {
      *error = "visual cancellation received outside a visual mode";
      return false;
    }
    SetMode(state, "normal");
    JsonValue::Object event =
        Event(*kind == "visual_empty" ? "visual_empty" : "visual_cancelled",
              browser_id);
    sink_->Emit(JsonValue(std::move(event)));
  } else if (*kind == "visual_yank") {
    if (state->mode != "visual") {
      *error = "visual_yank received outside visual mode";
      return false;
    }
    const auto text = message->String("text");
    if (!text || text->size() > 4 * 1024 * 1024) {
      *error = "visual yank text exceeds the 4 MiB limit";
      return false;
    }
    JsonValue::Object event = Event("visual_yank", browser_id);
    event.emplace("text", JsonValue(*text));
    sink_->Emit(JsonValue(std::move(event)));
    SetMode(state, "normal");
  } else if (*kind == "cursor_input_focused") {
    if (state->mode != "normal") {
      *error = "cursor input focus received outside normal mode";
      return false;
    }
    const std::string tag = message->String("tag").value_or("");
    if (tag.empty() || tag.size() > 32) {
      *error = "invalid cursor input target";
      return false;
    }
    SetMode(state, "insert");
    JsonValue::Object event = Event("cursor_input_focused", browser_id);
    event.emplace("tag", JsonValue(tag));
    sink_->Emit(JsonValue(std::move(event)));
  } else if (*kind == "cursor_input_unavailable") {
    if (state->mode != "normal") {
      *error = "cursor input result received outside normal mode";
      return false;
    }
    sink_->Emit(JsonValue(Event("cursor_input_unavailable", browser_id)));
  } else {
    *error = "unsupported bridge event";
    return false;
  }

  *response = "{\"ok\":true}";
  return true;
}

CefBrowserState* CefBrowserManager::Find(std::uint32_t browser_id) {
  const auto found = states_.find(browser_id);
  return found == states_.end() ? nullptr : &found->second;
}

const CefBrowserState* CefBrowserManager::Find(
    std::uint32_t browser_id) const {
  const auto found = states_.find(browser_id);
  return found == states_.end() ? nullptr : &found->second;
}

void CefBrowserManager::GetViewRect(std::uint32_t browser_id,
                                    CefRect* rect) const {
  const CefBrowserState* state = Find(browser_id);
  const int width = state ? state->width : 1;
  const int height = state ? state->height : 1;
  *rect = CefRect(0, 0, width, height);
}

void CefBrowserManager::GetScreenInfo(std::uint32_t browser_id,
                                      CefScreenInfo* info) const {
  const CefBrowserState* state = Find(browser_id);
  const int width = state ? state->width : 1;
  const int height = state ? state->height : 1;
  info->device_scale_factor = 1.0F;
  info->rect = CefRect(0, 0, width, height);
  info->available_rect = info->rect;
}

void CefBrowserManager::OnPopupShow(std::uint32_t browser_id, bool show) {
  CefBrowserState* state = Find(browser_id);
  if (!state || state->destroying) {
    return;
  }
  state->framebuffer.SetPopupVisible(show);
  std::string error;
  if (state->framebuffer.has_view() && !Render(state, &error)) {
    EmitError(browser_id, error);
  }
}

void CefBrowserManager::OnPopupSize(std::uint32_t browser_id,
                                    const CefRect& rect) {
  CefBrowserState* state = Find(browser_id);
  if (!state || state->destroying) {
    return;
  }
  state->framebuffer.SetPopupRect({rect.x, rect.y, rect.width, rect.height});
}

void CefBrowserManager::OnPaint(std::uint32_t browser_id,
                                CefRenderHandler::PaintElementType type,
                                const CefRenderHandler::RectList& dirty_rects,
                                const void* buffer,
                                int width,
                                int height) {
  CefBrowserState* state = Find(browser_id);
  if (!state || state->destroying) {
    return;
  }
  std::string error;
  bool converted = false;
  bool rendered = true;
  if (type == PET_VIEW) {
    std::vector<PixelRect> regions;
    const bool partial = state->framebuffer.has_view() &&
                         state->framebuffer.width() == width &&
                         state->framebuffer.height() == height &&
                         PrepareDirtyRects(*state, dirty_rects, width, height,
                                           &regions);
    if (partial) {
      converted = state->framebuffer.UpdateViewRects(
          buffer, width, height, regions, &error);
      if (converted) {
        rendered = RenderRegions(state, regions, &error);
      }
    } else {
      converted =
          state->framebuffer.UpdateView(buffer, width, height, &error);
      if (converted) {
        rendered = Render(state, &error);
      }
    }
  } else {
    std::vector<PixelRect> popup_regions;
    const bool partial = state->framebuffer.popup_width() == width &&
                         state->framebuffer.popup_height() == height &&
                         PrepareDirtyRects(*state, dirty_rects, width, height,
                                           &popup_regions);
    if (partial) {
      converted = state->framebuffer.UpdatePopupRects(
          buffer, width, height, popup_regions, &error);
    } else {
      converted =
          state->framebuffer.UpdatePopup(buffer, width, height, &error);
      popup_regions = {{0, 0, width, height}};
    }
    if (converted && state->framebuffer.popup_visible()) {
      std::vector<PixelRect> view_regions;
      view_regions.reserve(popup_regions.size());
      const PixelRect popup = state->framebuffer.popup_rect();
      for (const PixelRect region : popup_regions) {
        const PixelRect view = ClipPixelRect(
            {popup.x + region.x, popup.y + region.y, region.width,
             region.height},
            state->framebuffer.width(), state->framebuffer.height());
        if (view.width > 0 && view.height > 0) {
          view_regions.push_back(view);
        }
      }
      rendered = view_regions.empty() ? true
                                      : RenderRegions(state, view_regions,
                                                      &error);
    }
  }
  if (!converted || !rendered) {
    EmitError(browser_id, error);
  } else if (type == PET_VIEW &&
             (state->last_frame_width != width ||
              state->last_frame_height != height)) {
    state->last_frame_width = width;
    state->last_frame_height = height;
    JsonValue::Object event = Event("frame_ready", browser_id);
    event.emplace("width", JsonValue(width));
    event.emplace("height", JsonValue(height));
    sink_->Emit(JsonValue(std::move(event)));
  }
}

void CefBrowserManager::OnAfterCreated(std::uint32_t browser_id,
                                       CefRefPtr<CefBrowser> browser) {
  CefBrowserState* state = Find(browser_id);
  if (!state) {
    browser->GetHost()->CloseBrowser(true);
    return;
  }
  state->browser = browser;
  if (state->destroying) {
    browser->GetHost()->CloseBrowser(true);
    return;
  }
  browser->GetHost()->SetFocus(state->focused);
  browser->GetHost()->WasHidden(!state->visible);
  browser->GetHost()->WasResized();

  const std::string current_url = browser->GetMainFrame()->GetURL();
  if (!state->url.empty() && current_url != state->url) {
    browser->GetMainFrame()->LoadURL(state->url);
  }
  JsonValue::Object created = Event("created", browser_id);
  sink_->Emit(JsonValue(std::move(created)));
  JsonValue::Object changed = Event("url_changed", browser_id);
  changed.emplace("url", JsonValue(state->url));
  sink_->Emit(JsonValue(std::move(changed)));
}

void CefBrowserManager::OnBeforeClose(std::uint32_t browser_id) {
  const auto found = states_.find(browser_id);
  if (found == states_.end()) {
    return;
  }
  std::string ignored;
  CleanupKitty(&found->second, &ignored);
  found->second.browser = nullptr;
  found->second.client = nullptr;
  states_.erase(found);
  JsonValue::Object destroyed = Event("destroyed", browser_id);
  sink_->Emit(JsonValue(std::move(destroyed)));
  sink_->OnBrowserCountChanged();
}

void CefBrowserManager::OnAddressChange(std::uint32_t browser_id,
                                        std::string url) {
  CefBrowserState* state = Find(browser_id);
  if (!state || state->destroying) {
    return;
  }
  state->url = std::move(url);
  JsonValue::Object event = Event("url_changed", browser_id);
  event.emplace("url", JsonValue(state->url));
  sink_->Emit(JsonValue(std::move(event)));
}

void CefBrowserManager::OnTitleChange(std::uint32_t browser_id,
                                      std::string title) {
  CefBrowserState* state = Find(browser_id);
  if (!state || state->destroying) {
    return;
  }
  state->title = std::move(title);
  JsonValue::Object event = Event("title_changed", browser_id);
  event.emplace("title", JsonValue(state->title));
  sink_->Emit(JsonValue(std::move(event)));
}

void CefBrowserManager::OnLoadingStateChange(std::uint32_t browser_id,
                                             bool loading,
                                             bool can_go_back,
                                             bool can_go_forward) {
  CefBrowserState* state = Find(browser_id);
  if (!state) {
    return;
  }
  state->can_go_back = can_go_back;
  state->can_go_forward = can_go_forward;
  state->page_ready = !loading;
  if (loading && state->mode != "normal") {
    SetMode(state, "normal");
  }
  JsonValue::Object event = Event("loading", browser_id);
  event.emplace("loading", JsonValue(loading));
  event.emplace("can_go_back", JsonValue(can_go_back));
  event.emplace("can_go_forward", JsonValue(can_go_forward));
  event.emplace("url", JsonValue(state->url));
  sink_->Emit(JsonValue(std::move(event)));
  if (!loading) {
    while (!state->pending_navigation_commands.empty()) {
      std::string command =
          std::move(state->pending_navigation_commands.front());
      state->pending_navigation_commands.erase(
          state->pending_navigation_commands.begin());
      ExecuteNavigationCommand(state, command);
      if (!state->page_ready) {
        return;
      }
    }
    std::vector<std::string> scripts = std::move(state->pending_scripts);
    state->pending_scripts.clear();
    for (std::string& script : scripts) {
      ExecuteScript(state, std::move(script));
    }
    if (state->mode == "normal") {
      ExecuteScript(
          state,
          "window.__nvimBrowser&&window.__nvimBrowser.visual&&"
          "window.__nvimBrowser.visual.setNormalMode(true);");
    }
  }
}

void CefBrowserManager::ExecuteNavigationCommand(CefBrowserState* state,
                                                 std::string_view command) {
  if (!state || !state->browser) {
    return;
  }
  if (command == "back") {
    if (state->browser->CanGoBack()) {
      state->page_ready = false;
      state->browser->GoBack();
    }
  } else if (command == "forward") {
    if (state->browser->CanGoForward()) {
      state->page_ready = false;
      state->browser->GoForward();
    }
  } else if (command == "reload") {
    state->page_ready = false;
    state->browser->Reload();
  } else if (command == "stop") {
    state->browser->StopLoad();
  }
}

void CefBrowserManager::ExecuteScript(CefBrowserState* state,
                                      std::string script) {
  if (!state) {
    return;
  }
  if (!state->browser || !state->page_ready) {
    state->pending_scripts.push_back(std::move(script));
    return;
  }
  CefRefPtr<CefFrame> frame = state->browser->GetMainFrame();
  frame->ExecuteJavaScript(script, frame->GetURL(), 0);
}

void CefBrowserManager::SetMode(CefBrowserState* state, std::string mode) {
  if (!state || state->mode == mode) {
    return;
  }
  state->mode = std::move(mode);
  JsonValue::Object event = Event("mode_changed", state->browser_id);
  event.emplace("mode", JsonValue(state->mode));
  sink_->Emit(JsonValue(std::move(event)));
  ExecuteScript(
      state,
      "window.__nvimBrowser&&window.__nvimBrowser.visual&&"
      "window.__nvimBrowser.visual.setNormalMode(" +
          std::string(state->mode == "normal" ? "true" : "false") + ");");
}

bool CefBrowserManager::Render(CefBrowserState* state, std::string* error) {
  if (!state->framebuffer.has_view()) {
    return true;
  }
  if (!state->attached) {
    return true;
  }
  if (!renderer_->UploadRgba(
          state->browser_id, state->browser_image_id,
          state->framebuffer.width(), state->framebuffer.height(),
          state->framebuffer.composited(), error)) {
    return false;
  }
  return renderer_->PlaceRelative(
      state->browser_image_id, state->browser_placement_id,
      state->anchor_image_id, state->anchor_placement_id, state->columns,
      state->rows, error);
}

bool CefBrowserManager::RenderRegions(CefBrowserState* state,
                                      std::span<const PixelRect> rects,
                                      std::string* error) {
  if (!state->attached || !state->framebuffer.has_view()) {
    return true;
  }
  std::vector<std::uint8_t> rgba;
  for (const PixelRect requested : rects) {
    const PixelRect rect = ClipPixelRect(
        requested, state->framebuffer.width(), state->framebuffer.height());
    if (rect.width < 1 || rect.height < 1) {
      continue;
    }
    if (!state->framebuffer.Extract(rect, &rgba, error) ||
        !renderer_->UploadRgbaRegion(
            state->browser_id, state->browser_image_id,
            state->framebuffer.width(), state->framebuffer.height(), rect.x,
            rect.y, rect.width, rect.height, rgba, error)) {
      return false;
    }
  }
  return true;
}

bool CefBrowserManager::CleanupKitty(CefBrowserState* state,
                                     std::string* error) {
  bool success = true;
  std::string local_error;
  if (state->attached &&
      !renderer_->DeletePlacement(state->browser_image_id,
                                  state->browser_placement_id,
                                  &local_error)) {
    success = false;
  }
  state->attached = false;
  if (!renderer_->DeleteImage(state->browser_image_id, &local_error)) {
    success = false;
  }
  if (!renderer_->DeleteImage(state->anchor_image_id, &local_error)) {
    success = false;
  }
  if (!success && error) {
    *error = local_error;
  }
  return success;
}

void CefBrowserManager::EmitError(std::uint32_t browser_id,
                                  std::string message) {
  JsonValue::Object event = Event("error", browser_id);
  event.emplace("message", JsonValue(std::move(message)));
  sink_->Emit(JsonValue(std::move(event)));
}

}  // namespace browser
