#include "framebuffer.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

int main() {
  const std::array<std::uint8_t, 8> bgra = {
      1, 2, 3, 4,
      10, 20, 30, 40,
  };
  std::vector<std::uint8_t> rgba;
  std::string error;
  assert(browser::PixelConverter::BgraToRgba(bgra.data(), 2, 1, &rgba, &error));
  const std::vector<std::uint8_t> expected = {
      3, 2, 1, 4,
      30, 20, 10, 40,
  };
  assert(rgba == expected);

  browser::Framebuffer framebuffer;
  const std::array<std::uint8_t, 16> view = {
      0, 0, 0, 255, 0, 0, 0, 255,
      0, 0, 0, 255, 0, 0, 0, 255,
  };
  assert(framebuffer.UpdateView(view.data(), 2, 2, &error));
  framebuffer.SetPopupRect({1, 0, 1, 1});
  framebuffer.SetPopupVisible(true);
  const std::array<std::uint8_t, 4> popup = {7, 8, 9, 255};
  assert(framebuffer.UpdatePopup(popup.data(), 1, 1, &error));
  const auto composited = framebuffer.composited();
  assert(composited.size() == 16);
  assert(composited[4] == 9);
  assert(composited[5] == 8);
  assert(composited[6] == 7);

  framebuffer.SetPopupVisible(false);
  assert(framebuffer.composited()[4] == 0);

  const std::array<std::uint8_t, 24> changed = {
      0, 0, 0, 255, 1, 2, 3, 4, 0, 0, 0, 255,
      0, 0, 0, 255, 5, 6, 7, 8, 0, 0, 0, 255,
  };
  browser::Framebuffer dirty;
  const std::array<std::uint8_t, 24> blank = {
      0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255,
      0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255,
  };
  assert(dirty.UpdateView(blank.data(), 3, 2, &error));
  const std::array<browser::PixelRect, 1> update = {{{1, 0, 1, 2}}};
  assert(dirty.UpdateViewRects(changed.data(), 3, 2, update, &error));
  std::vector<std::uint8_t> extracted;
  assert(dirty.Extract(update[0], &extracted, &error));
  const std::vector<std::uint8_t> extracted_expected = {
      3, 2, 1, 4,
      7, 6, 5, 8,
  };
  assert(extracted == extracted_expected);
  assert(dirty.composited()[0] == 0);
  assert(dirty.composited()[4] == 3);
  assert(dirty.composited()[20] == 0);

  const browser::PixelRect clipped =
      browser::ClipPixelRect({-5, 1, 8, 4}, 10, 3);
  assert(clipped.x == 0 && clipped.y == 1 && clipped.width == 3 &&
         clipped.height == 2);
  const std::array<browser::PixelRect, 3> merge_input = {
      browser::PixelRect{0, 0, 2, 2}, browser::PixelRect{2, 0, 2, 2},
      browser::PixelRect{8, 8, 1, 1}};
  const auto merged = browser::MergePixelRects(merge_input);
  assert(merged.size() == 2);
  assert(merged[0].x == 0 && merged[0].width == 4);
  return 0;
}
