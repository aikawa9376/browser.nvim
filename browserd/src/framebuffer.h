#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace browser {

struct PixelRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

PixelRect ClipPixelRect(PixelRect rect, int width, int height);
std::vector<PixelRect> MergePixelRects(std::span<const PixelRect> rects);

class PixelConverter {
 public:
  static bool BgraToRgba(const void* bgra,
                         int width,
                         int height,
                         std::vector<std::uint8_t>* rgba,
                         std::string* error);
  static bool BgraRectsToRgba(const void* bgra,
                              int width,
                              int height,
                              std::span<const PixelRect> rects,
                              std::vector<std::uint8_t>* rgba,
                              std::string* error);
};

class Framebuffer {
 public:
  bool UpdateView(const void* bgra,
                  int width,
                  int height,
                  std::string* error);
  bool UpdatePopup(const void* bgra,
                   int width,
                   int height,
                   std::string* error);
  bool UpdateViewRects(const void* bgra,
                       int width,
                       int height,
                       std::span<const PixelRect> rects,
                       std::string* error);
  bool UpdatePopupRects(const void* bgra,
                        int width,
                        int height,
                        std::span<const PixelRect> rects,
                        std::string* error);
  bool Extract(PixelRect rect,
               std::vector<std::uint8_t>* rgba,
               std::string* error) const;
  std::vector<PixelRect> SetMasks(std::span<const PixelRect> masks,
                                  int width,
                                  int height);
  void SetPopupRect(PixelRect rect);
  void SetPopupVisible(bool visible);

  [[nodiscard]] bool has_view() const { return !view_rgba_.empty(); }
  [[nodiscard]] int width() const { return width_; }
  [[nodiscard]] int height() const { return height_; }
  [[nodiscard]] int popup_width() const { return popup_width_; }
  [[nodiscard]] int popup_height() const { return popup_height_; }
  [[nodiscard]] bool popup_visible() const { return popup_visible_; }
  [[nodiscard]] PixelRect popup_rect() const { return popup_rect_; }
  [[nodiscard]] std::span<const PixelRect> masks() const { return masks_; }
  [[nodiscard]] std::span<const std::uint8_t> composited() const {
    return composited_rgba_;
  }

 private:
  void Compose();

  int width_ = 0;
  int height_ = 0;
  int popup_width_ = 0;
  int popup_height_ = 0;
  bool popup_visible_ = false;
  PixelRect popup_rect_;
  std::vector<std::uint8_t> view_rgba_;
  std::vector<std::uint8_t> popup_rgba_;
  std::vector<std::uint8_t> composited_rgba_;
  std::vector<PixelRect> masks_;
};

}  // namespace browser
