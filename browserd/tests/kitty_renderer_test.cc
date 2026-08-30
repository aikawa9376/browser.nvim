#include "kitty_renderer.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

int main() {
  using browser::KittyRenderer;

  assert(KittyRenderer::Base64("/browser") == "L2Jyb3dzZXI=");

  const std::string graphics = KittyRenderer::GraphicsCommand("a=p,i=1,q=2");
  assert(graphics.starts_with("\x1b_G"));
  assert(graphics.ends_with("\x1b\\"));

  const std::string wrapped = KittyRenderer::WrapForTmux(graphics);
  assert(wrapped.starts_with("\x1bPtmux;\x1b\x1b_G"));
  assert(wrapped.ends_with("\x1b\\"));

  const auto gradient = KittyRenderer::Gradient(3, 2);
  assert(gradient.size() == 3U * 2U * 4U);
  assert(gradient[3] == 255);
  assert(gradient[gradient.size() - 1] == 255);
  assert(KittyRenderer::Gradient(0, 1).empty());

  assert(KittyRenderer::FrameUpdateControl(19, 2, 3, 4, 5, 80) ==
         "a=f,f=32,t=s,s=4,v=5,S=80,i=19,q=2,x=2,y=3,r=1,X=1");
  KittyRenderer dry_run(true);
  std::vector<std::uint8_t> region(4U * 5U * 4U, 255);
  std::string error;
  assert(dry_run.UploadRgbaRegion(1, 19, 20, 20, 2, 3, 4, 5, region,
                                  &error));
  assert(!dry_run.UploadRgbaRegion(1, 19, 5, 5, 3, 3, 4, 5, region,
                                   &error));
  return 0;
}
