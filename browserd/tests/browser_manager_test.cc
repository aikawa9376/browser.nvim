#include "browser_manager.h"

#include <cassert>
#include <string>

namespace {

browser::JsonValue Message(std::string type) {
  browser::JsonValue::Object object;
  object.emplace("type", std::move(type));
  object.emplace("browser_id", 1);
  return browser::JsonValue(std::move(object));
}

browser::JsonValue CreateMessage() {
  browser::JsonValue::Object object;
  object.emplace("type", "create");
  object.emplace("browser_id", 1);
  object.emplace("url", "https://example.com");
  object.emplace("cols", 80);
  object.emplace("rows", 24);
  object.emplace("width", 800);
  object.emplace("height", 480);
  object.emplace("fps", 60);
  object.emplace("anchor_image_id", 101);
  object.emplace("anchor_placement_id", 102);
  object.emplace("browser_image_id", 103);
  object.emplace("browser_placement_id", 104);
  return browser::JsonValue(std::move(object));
}

}  // namespace

int main() {
  browser::KittyRenderer renderer(true);
  browser::BrowserManager manager(&renderer);
  std::string error;

  assert(manager.Create(CreateMessage(), &error));
  assert(manager.Find(1) != nullptr);
  assert(manager.Find(1)->pixels.size() == 800U * 480U * 4U);

  browser::JsonValue::Object attach;
  attach.emplace("type", "attach");
  attach.emplace("browser_id", 1);
  attach.emplace("cols", 80);
  attach.emplace("rows", 24);
  attach.emplace("full_frame", true);
  assert(manager.Attach(browser::JsonValue(std::move(attach)), &error));
  assert(manager.Find(1)->attached);

  browser::JsonValue::Object resize;
  resize.emplace("type", "resize");
  resize.emplace("browser_id", 1);
  resize.emplace("cols", 40);
  resize.emplace("rows", 20);
  resize.emplace("width", 400);
  resize.emplace("height", 400);
  assert(manager.Resize(browser::JsonValue(std::move(resize)), &error));
  assert(manager.Find(1)->pixels.size() == 400U * 400U * 4U);

  assert(manager.Detach(1, &error));
  assert(!manager.Find(1)->attached);
  assert(manager.Destroy(1, &error));
  assert(manager.Find(1) == nullptr);
  return 0;
}
