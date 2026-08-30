#pragma once

#include "json.h"
#include "kitty_renderer.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace browser {

struct BrowserState {
  std::uint32_t browser_id = 0;
  std::uint32_t anchor_image_id = 0;
  std::uint32_t anchor_placement_id = 0;
  std::uint32_t browser_image_id = 0;
  std::uint32_t browser_placement_id = 0;
  std::string url;
  int columns = 1;
  int rows = 1;
  int width = 10;
  int height = 20;
  int fps = 60;
  bool attached = false;
  bool visible = false;
  bool focused = false;
  std::string mode = "normal";
  std::vector<std::uint8_t> pixels;
};

class BrowserManager {
 public:
  explicit BrowserManager(KittyRenderer* renderer) : renderer_(renderer) {}
  ~BrowserManager();

  bool Create(const JsonValue& message, std::string* error);
  bool Resize(const JsonValue& message, std::string* error);
  bool Attach(const JsonValue& message, std::string* error);
  bool Detach(std::uint32_t browser_id, std::string* error);
  bool Destroy(std::uint32_t browser_id, std::string* error);
  void DestroyAll();

  BrowserState* Find(std::uint32_t browser_id);
  const BrowserState* Find(std::uint32_t browser_id) const;

 private:
  bool UploadFullFrame(BrowserState* state, bool refresh_anchor,
                       std::string* error);
  static bool ReadPositive(const JsonValue& message, std::string_view key,
                           std::uint32_t* output, std::string* error);
  static bool ReadPositiveInt(const JsonValue& message, std::string_view key,
                              int* output, std::string* error);

  KittyRenderer* renderer_;
  std::map<std::uint32_t, BrowserState> states_;
};

}  // namespace browser
