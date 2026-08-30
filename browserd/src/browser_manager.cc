#include "browser_manager.h"

#include <limits>
#include <utility>

namespace browser {

BrowserManager::~BrowserManager() { DestroyAll(); }

bool BrowserManager::ReadPositive(const JsonValue& message,
                                  std::string_view key,
                                  std::uint32_t* output,
                                  std::string* error) {
  const auto value = message.Integer(key);
  if (!value || *value < 1 ||
      *value > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
    if (error) {
      *error = std::string(key) + " must be a positive uint32";
    }
    return false;
  }
  *output = static_cast<std::uint32_t>(*value);
  return true;
}

bool BrowserManager::ReadPositiveInt(const JsonValue& message,
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

bool BrowserManager::UploadFullFrame(BrowserState* state,
                                     bool refresh_anchor,
                                     std::string* error) {
  if (refresh_anchor &&
      !renderer_->UploadAnchor(state->browser_id, state->anchor_image_id,
                               state->anchor_placement_id, error)) {
    return false;
  }
  state->pixels = KittyRenderer::Gradient(state->width, state->height);
  if (state->pixels.empty()) {
    if (error) {
      *error = "failed to allocate gradient framebuffer";
    }
    return false;
  }
  return renderer_->UploadRgba(state->browser_id, state->browser_image_id,
                               state->width, state->height, state->pixels,
                               error);
}

bool BrowserManager::Create(const JsonValue& message, std::string* error) {
  BrowserState state;
  if (!ReadPositive(message, "browser_id", &state.browser_id, error) ||
      !ReadPositive(message, "anchor_image_id", &state.anchor_image_id, error) ||
      !ReadPositive(message, "anchor_placement_id", &state.anchor_placement_id,
                    error) ||
      !ReadPositive(message, "browser_image_id", &state.browser_image_id,
                    error) ||
      !ReadPositive(message, "browser_placement_id",
                    &state.browser_placement_id, error) ||
      !ReadPositiveInt(message, "cols", &state.columns, error) ||
      !ReadPositiveInt(message, "rows", &state.rows, error) ||
      !ReadPositiveInt(message, "width", &state.width, error) ||
      !ReadPositiveInt(message, "height", &state.height, error)) {
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
  if (states_.contains(state.browser_id)) {
    if (error) {
      *error = "browser_id already exists";
    }
    return false;
  }
  if (!UploadFullFrame(&state, true, error)) {
    std::string ignored;
    renderer_->DeleteImage(state.browser_image_id, &ignored);
    renderer_->DeleteImage(state.anchor_image_id, &ignored);
    return false;
  }
  states_.emplace(state.browser_id, std::move(state));
  return true;
}

bool BrowserManager::Resize(const JsonValue& message, std::string* error) {
  std::uint32_t browser_id = 0;
  if (!ReadPositive(message, "browser_id", &browser_id, error)) {
    return false;
  }
  BrowserState* state = Find(browser_id);
  if (!state) {
    if (error) {
      *error = "unknown browser_id";
    }
    return false;
  }

  int columns = 0;
  int rows = 0;
  int width = 0;
  int height = 0;
  if (!ReadPositiveInt(message, "cols", &columns, error) ||
      !ReadPositiveInt(message, "rows", &rows, error) ||
      !ReadPositiveInt(message, "width", &width, error) ||
      !ReadPositiveInt(message, "height", &height, error)) {
    return false;
  }
  const bool pixels_changed = state->width != width || state->height != height;
  const bool cells_changed = state->columns != columns || state->rows != rows;
  state->columns = columns;
  state->rows = rows;
  state->width = width;
  state->height = height;

  if (pixels_changed && !UploadFullFrame(state, false, error)) {
    return false;
  }
  if (state->attached && (pixels_changed || cells_changed)) {
    return renderer_->PlaceRelative(
        state->browser_image_id, state->browser_placement_id,
        state->anchor_image_id, state->anchor_placement_id, state->columns,
        state->rows, error);
  }
  return true;
}

bool BrowserManager::Attach(const JsonValue& message, std::string* error) {
  std::uint32_t browser_id = 0;
  if (!ReadPositive(message, "browser_id", &browser_id, error)) {
    return false;
  }
  BrowserState* state = Find(browser_id);
  if (!state) {
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

  const bool full_frame = message.Boolean("full_frame").value_or(false);
  if (full_frame && !UploadFullFrame(state, true, error)) {
    return false;
  }
  if (!renderer_->PlaceRelative(
          state->browser_image_id, state->browser_placement_id,
          state->anchor_image_id, state->anchor_placement_id, state->columns,
          state->rows, error)) {
    return false;
  }
  state->attached = true;
  return true;
}

bool BrowserManager::Detach(std::uint32_t browser_id, std::string* error) {
  BrowserState* state = Find(browser_id);
  if (!state) {
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

bool BrowserManager::Destroy(std::uint32_t browser_id, std::string* error) {
  const auto found = states_.find(browser_id);
  if (found == states_.end()) {
    return true;
  }
  BrowserState& state = found->second;
  bool success = true;
  std::string local_error;
  if (state.attached &&
      !renderer_->DeletePlacement(state.browser_image_id,
                                  state.browser_placement_id, &local_error)) {
    success = false;
  }
  if (!renderer_->DeleteImage(state.browser_image_id, &local_error)) {
    success = false;
  }
  if (!renderer_->DeleteImage(state.anchor_image_id, &local_error)) {
    success = false;
  }
  states_.erase(found);
  if (!success && error) {
    *error = local_error;
  }
  return success;
}

void BrowserManager::DestroyAll() {
  while (!states_.empty()) {
    std::string ignored;
    Destroy(states_.begin()->first, &ignored);
  }
}

BrowserState* BrowserManager::Find(std::uint32_t browser_id) {
  const auto found = states_.find(browser_id);
  return found == states_.end() ? nullptr : &found->second;
}

const BrowserState* BrowserManager::Find(std::uint32_t browser_id) const {
  const auto found = states_.find(browser_id);
  return found == states_.end() ? nullptr : &found->second;
}

}  // namespace browser
