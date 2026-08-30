#include "framebuffer.h"

#include <algorithm>
#include <limits>

namespace browser {
namespace {

bool PixelBytes(int width, int height, std::size_t* output) {
  if (width < 1 || height < 1 || width > 16384 || height > 16384) {
    return false;
  }
  const std::uint64_t bytes = static_cast<std::uint64_t>(width) *
                              static_cast<std::uint64_t>(height) * 4;
  if (bytes > 1024ULL * 1024ULL * 1024ULL ||
      bytes > std::numeric_limits<std::size_t>::max()) {
    return false;
  }
  *output = static_cast<std::size_t>(bytes);
  return true;
}

}  // namespace

PixelRect ClipPixelRect(PixelRect rect, int width, int height) {
  const int left = std::clamp(rect.x, 0, std::max(0, width));
  const int top = std::clamp(rect.y, 0, std::max(0, height));
  const std::int64_t requested_right =
      static_cast<std::int64_t>(rect.x) + std::max(0, rect.width);
  const std::int64_t requested_bottom =
      static_cast<std::int64_t>(rect.y) + std::max(0, rect.height);
  const int right = static_cast<int>(std::clamp<std::int64_t>(
      requested_right, left, std::max(left, width)));
  const int bottom = static_cast<int>(std::clamp<std::int64_t>(
      requested_bottom, top, std::max(top, height)));
  return {left, top, right - left, bottom - top};
}

std::vector<PixelRect> MergePixelRects(std::span<const PixelRect> rects) {
  std::vector<PixelRect> merged;
  for (const PixelRect rect : rects) {
    if (rect.width < 1 || rect.height < 1) {
      continue;
    }
    PixelRect candidate = rect;
    bool changed = true;
    while (changed) {
      changed = false;
      for (auto iterator = merged.begin(); iterator != merged.end();) {
        const std::int64_t candidate_right =
            static_cast<std::int64_t>(candidate.x) + candidate.width;
        const std::int64_t candidate_bottom =
            static_cast<std::int64_t>(candidate.y) + candidate.height;
        const std::int64_t existing_right =
            static_cast<std::int64_t>(iterator->x) + iterator->width;
        const std::int64_t existing_bottom =
            static_cast<std::int64_t>(iterator->y) + iterator->height;
        const bool touches =
            candidate.x <= existing_right && iterator->x <= candidate_right &&
            candidate.y <= existing_bottom && iterator->y <= candidate_bottom;
        if (!touches) {
          ++iterator;
          continue;
        }
        const int left = std::min(candidate.x, iterator->x);
        const int top = std::min(candidate.y, iterator->y);
        const std::int64_t right = std::max(candidate_right, existing_right);
        const std::int64_t bottom =
            std::max(candidate_bottom, existing_bottom);
        candidate = {left, top, static_cast<int>(right - left),
                     static_cast<int>(bottom - top)};
        iterator = merged.erase(iterator);
        changed = true;
      }
    }
    merged.push_back(candidate);
  }
  return merged;
}

bool PixelConverter::BgraToRgba(const void* bgra,
                                int width,
                                int height,
                                std::vector<std::uint8_t>* rgba,
                                std::string* error) {
  std::size_t size = 0;
  if (!bgra || !rgba || !PixelBytes(width, height, &size)) {
    if (error) {
      *error = "invalid BGRA framebuffer";
    }
    return false;
  }
  const auto* source = static_cast<const std::uint8_t*>(bgra);
  rgba->resize(size);
  for (std::size_t offset = 0; offset < size; offset += 4) {
    (*rgba)[offset] = source[offset + 2];
    (*rgba)[offset + 1] = source[offset + 1];
    (*rgba)[offset + 2] = source[offset];
    (*rgba)[offset + 3] = source[offset + 3];
  }
  return true;
}

bool PixelConverter::BgraRectsToRgba(
    const void* bgra,
    int width,
    int height,
    std::span<const PixelRect> rects,
    std::vector<std::uint8_t>* rgba,
    std::string* error) {
  std::size_t size = 0;
  if (!bgra || !rgba || !PixelBytes(width, height, &size) ||
      rgba->size() != size) {
    if (error) {
      *error = "invalid BGRA dirty-rectangle framebuffer";
    }
    return false;
  }
  const auto* source = static_cast<const std::uint8_t*>(bgra);
  for (const PixelRect requested : rects) {
    const PixelRect rect = ClipPixelRect(requested, width, height);
    for (int y = rect.y; y < rect.y + rect.height; ++y) {
      for (int x = rect.x; x < rect.x + rect.width; ++x) {
        const std::size_t offset =
            (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
             static_cast<std::size_t>(x)) *
            4;
        (*rgba)[offset] = source[offset + 2];
        (*rgba)[offset + 1] = source[offset + 1];
        (*rgba)[offset + 2] = source[offset];
        (*rgba)[offset + 3] = source[offset + 3];
      }
    }
  }
  return true;
}

bool Framebuffer::UpdateView(const void* bgra,
                             int width,
                             int height,
                             std::string* error) {
  if (!PixelConverter::BgraToRgba(bgra, width, height, &view_rgba_, error)) {
    return false;
  }
  width_ = width;
  height_ = height;
  Compose();
  return true;
}

bool Framebuffer::UpdatePopup(const void* bgra,
                              int width,
                              int height,
                              std::string* error) {
  if (!PixelConverter::BgraToRgba(bgra, width, height, &popup_rgba_, error)) {
    return false;
  }
  popup_width_ = width;
  popup_height_ = height;
  Compose();
  return true;
}

bool Framebuffer::UpdateViewRects(const void* bgra,
                                  int width,
                                  int height,
                                  std::span<const PixelRect> rects,
                                  std::string* error) {
  if (width != width_ || height != height_ || view_rgba_.empty()) {
    if (error) {
      *error = "dirty view update requires an initialized matching view";
    }
    return false;
  }
  if (!PixelConverter::BgraRectsToRgba(bgra, width, height, rects,
                                       &view_rgba_, error)) {
    return false;
  }
  Compose();
  return true;
}

bool Framebuffer::UpdatePopupRects(const void* bgra,
                                   int width,
                                   int height,
                                   std::span<const PixelRect> rects,
                                   std::string* error) {
  if (width != popup_width_ || height != popup_height_ || popup_rgba_.empty()) {
    if (error) {
      *error = "dirty popup update requires an initialized matching popup";
    }
    return false;
  }
  if (!PixelConverter::BgraRectsToRgba(bgra, width, height, rects,
                                       &popup_rgba_, error)) {
    return false;
  }
  Compose();
  return true;
}

bool Framebuffer::Extract(PixelRect requested,
                          std::vector<std::uint8_t>* rgba,
                          std::string* error) const {
  const PixelRect rect = ClipPixelRect(requested, width_, height_);
  std::size_t size = 0;
  if (!rgba || rect.width != requested.width || rect.height != requested.height ||
      !PixelBytes(rect.width, rect.height, &size) || composited_rgba_.empty()) {
    if (error) {
      *error = "invalid framebuffer extraction rectangle";
    }
    return false;
  }
  rgba->resize(size);
  for (int row = 0; row < rect.height; ++row) {
    const std::size_t source =
        (static_cast<std::size_t>(rect.y + row) *
             static_cast<std::size_t>(width_) +
         static_cast<std::size_t>(rect.x)) *
        4;
    const std::size_t destination =
        static_cast<std::size_t>(row) * static_cast<std::size_t>(rect.width) *
        4;
    std::copy_n(composited_rgba_.begin() +
                    static_cast<std::ptrdiff_t>(source),
                static_cast<std::size_t>(rect.width) * 4,
                rgba->begin() + static_cast<std::ptrdiff_t>(destination));
  }
  return true;
}

void Framebuffer::SetPopupRect(PixelRect rect) {
  popup_rect_ = rect;
  Compose();
}

void Framebuffer::SetPopupVisible(bool visible) {
  popup_visible_ = visible;
  Compose();
}

void Framebuffer::Compose() {
  composited_rgba_ = view_rgba_;
  if (!popup_visible_ || composited_rgba_.empty() || popup_rgba_.empty() ||
      popup_width_ < 1 || popup_height_ < 1) {
    return;
  }

  const int copy_width = std::min(popup_width_, popup_rect_.width);
  const int copy_height = std::min(popup_height_, popup_rect_.height);
  for (int popup_y = 0; popup_y < copy_height; ++popup_y) {
    const int view_y = popup_rect_.y + popup_y;
    if (view_y < 0 || view_y >= height_) {
      continue;
    }
    for (int popup_x = 0; popup_x < copy_width; ++popup_x) {
      const int view_x = popup_rect_.x + popup_x;
      if (view_x < 0 || view_x >= width_) {
        continue;
      }
      const std::size_t source =
          (static_cast<std::size_t>(popup_y) *
               static_cast<std::size_t>(popup_width_) +
           static_cast<std::size_t>(popup_x)) *
          4;
      const std::size_t destination =
          (static_cast<std::size_t>(view_y) * static_cast<std::size_t>(width_) +
           static_cast<std::size_t>(view_x)) *
          4;
      std::copy_n(popup_rgba_.begin() + static_cast<std::ptrdiff_t>(source), 4,
                  composited_rgba_.begin() +
                      static_cast<std::ptrdiff_t>(destination));
    }
  }
}

}  // namespace browser
