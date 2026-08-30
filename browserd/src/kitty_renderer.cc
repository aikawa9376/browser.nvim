#include "kitty_renderer.h"

#include "shm.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <optional>
#include <sstream>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

namespace browser {
namespace {

bool EnabledEnvironment(const char* name) {
  const char* value = std::getenv(name);
  return value && (std::strcmp(value, "1") == 0 ||
                   std::strcmp(value, "true") == 0 ||
                   std::strcmp(value, "on") == 0);
}

std::optional<int> PositiveEnvironment(const char* name) {
  const char* value = std::getenv(name);
  if (!value || *value == '\0') {
    return std::nullopt;
  }
  char* end = nullptr;
  const long parsed = std::strtol(value, &end, 10);
  if (*end != '\0' || parsed < 1 || parsed > 1000) {
    return std::nullopt;
  }
  return static_cast<int>(parsed);
}

bool CheckedRgbaSize(int width, int height, std::size_t* size) {
  if (width < 1 || height < 1 || width > 16384 || height > 16384) {
    return false;
  }
  const std::uint64_t bytes = static_cast<std::uint64_t>(width) *
                              static_cast<std::uint64_t>(height) * 4;
  if (bytes > 1024ULL * 1024ULL * 1024ULL ||
      bytes > std::numeric_limits<std::size_t>::max()) {
    return false;
  }
  *size = static_cast<std::size_t>(bytes);
  return true;
}

int OpenTerminal(std::string* error) {
  std::vector<std::string> candidates = {"/dev/tty"};
  if (const char* configured = std::getenv("BROWSER_TTY");
      configured && *configured != '\0') {
    candidates.emplace_back(configured);
  }

#if defined(__linux__)
  // Neovim can own a perfectly valid TUI PTY without being the session's
  // controlling process (for example when embedded by an agent or launcher).
  // vim.system() replaces the child's stdout with the JSON IPC pipe, so open
  // the parent Neovim UI descriptor explicitly in that case.
  const std::string parent = "/proc/" + std::to_string(getppid()) + "/fd/";
  candidates.push_back(parent + "1");
  candidates.push_back(parent + "2");
  candidates.push_back(parent + "0");
#endif

  std::string last_error;
  for (const std::string& candidate : candidates) {
    const int descriptor = open(candidate.c_str(), O_WRONLY | O_CLOEXEC);
    if (descriptor < 0) {
      last_error = candidate + ": " + std::strerror(errno);
      continue;
    }
    if (isatty(descriptor) != 0) {
      return descriptor;
    }
    close(descriptor);
    last_error = candidate + ": not a TTY";
  }
  if (error) {
    *error = "unable to open Neovim's TTY (last attempt: " + last_error + ")";
  }
  return -1;
}

}  // namespace

KittyRenderer::KittyRenderer(bool dry_run)
    : dry_run_(dry_run || EnabledEnvironment("BROWSER_KITTY_DRY_RUN")),
      in_tmux_(std::getenv("TMUX") != nullptr && *std::getenv("TMUX") != '\0') {
  if (!dry_run_) {
    tty_fd_ = OpenTerminal(&error_);
  }
  DetectMetrics();
}

KittyRenderer::~KittyRenderer() {
  if (tty_fd_ >= 0) {
    close(tty_fd_);
  }
}

void KittyRenderer::DetectMetrics() {
  winsize size{};
  if (tty_fd_ >= 0 && ioctl(tty_fd_, TIOCGWINSZ, &size) == 0) {
    metrics_.columns = size.ws_col;
    metrics_.rows = size.ws_row;
    metrics_.pixel_width = size.ws_xpixel;
    metrics_.pixel_height = size.ws_ypixel;
    if (size.ws_col > 0 && size.ws_row > 0 && size.ws_xpixel > 0 &&
        size.ws_ypixel > 0) {
      metrics_.cell_width = std::max(1, size.ws_xpixel / size.ws_col);
      metrics_.cell_height = std::max(1, size.ws_ypixel / size.ws_row);
      metrics_.source = "ioctl";
    }
  }

  const auto configured_width = PositiveEnvironment("BROWSER_CELL_WIDTH");
  const auto configured_height = PositiveEnvironment("BROWSER_CELL_HEIGHT");
  if (configured_width) {
    metrics_.cell_width = *configured_width;
  }
  if (configured_height) {
    metrics_.cell_height = *configured_height;
  }
  if (configured_width || configured_height) {
    metrics_.source = "environment";
  }
}

std::string KittyRenderer::Base64(std::string_view input) {
  static constexpr char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string output;
  output.reserve(((input.size() + 2) / 3) * 4);
  for (std::size_t offset = 0; offset < input.size(); offset += 3) {
    const std::uint32_t first = static_cast<unsigned char>(input[offset]);
    const std::uint32_t second = offset + 1 < input.size()
                                     ? static_cast<unsigned char>(input[offset + 1])
                                     : 0;
    const std::uint32_t third = offset + 2 < input.size()
                                    ? static_cast<unsigned char>(input[offset + 2])
                                    : 0;
    const std::uint32_t value = (first << 16) | (second << 8) | third;
    output.push_back(alphabet[(value >> 18) & 0x3f]);
    output.push_back(alphabet[(value >> 12) & 0x3f]);
    output.push_back(offset + 1 < input.size() ? alphabet[(value >> 6) & 0x3f]
                                               : '=');
    output.push_back(offset + 2 < input.size() ? alphabet[value & 0x3f] : '=');
  }
  return output;
}

std::string KittyRenderer::GraphicsCommand(std::string_view control,
                                           std::string_view payload) {
  std::string command = "\x1b_G";
  command += control;
  if (!payload.empty()) {
    command.push_back(';');
    command += payload;
  }
  command += "\x1b\\";
  return command;
}

std::string KittyRenderer::WrapForTmux(std::string_view command) {
  std::string wrapped = "\x1bPtmux;";
  wrapped.reserve(command.size() + 16);
  for (const char byte : command) {
    if (byte == '\x1b') {
      wrapped.push_back('\x1b');
    }
    wrapped.push_back(byte);
  }
  wrapped += "\x1b\\";
  return wrapped;
}

std::vector<std::uint8_t> KittyRenderer::Gradient(int width, int height) {
  std::size_t size = 0;
  if (!CheckedRgbaSize(width, height, &size)) {
    return {};
  }
  std::vector<std::uint8_t> pixels(size);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const std::size_t offset =
          (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
           static_cast<std::size_t>(x)) *
          4;
      pixels[offset] = static_cast<std::uint8_t>(
          width == 1 ? 80 : (255LL * x) / (width - 1));
      pixels[offset + 1] = static_cast<std::uint8_t>(
          height == 1 ? 80 : (255LL * y) / (height - 1));
      pixels[offset + 2] = static_cast<std::uint8_t>(
          64 + (((x / 24) + (y / 24)) % 2) * 96);
      pixels[offset + 3] = 255;
    }
  }
  return pixels;
}

bool KittyRenderer::Write(std::string command, std::string* error) {
  if (dry_run_) {
    return true;
  }
  if (tty_fd_ < 0) {
    if (error) {
      *error = error_;
    }
    return false;
  }
  if (in_tmux_) {
    command = WrapForTmux(command);
  }

  // Graphics commands are intentionally short. One write keeps Neovim TUI
  // output from being interleaved inside the Kitty APC (or tmux DCS wrapper).
  const ssize_t written = write(tty_fd_, command.data(), command.size());
  if (written != static_cast<ssize_t>(command.size())) {
    if (error) {
      *error = written < 0 ? std::string("write /dev/tty: ") + std::strerror(errno)
                          : "short write to /dev/tty";
    }
    return false;
  }
  return true;
}

bool KittyRenderer::Transmit(std::uint32_t browser_id,
                             std::uint32_t image_id,
                             int width,
                             int height,
                             std::span<const std::uint8_t> rgba,
                             std::string_view extra_control,
                             std::string* error) {
  std::size_t expected = 0;
  if (!CheckedRgbaSize(width, height, &expected) || rgba.size() != expected) {
    if (error) {
      *error = "invalid RGBA dimensions or byte count";
    }
    return false;
  }
  if (dry_run_) {
    return true;
  }

  const std::string name = "/browser-" + std::to_string(getpid()) + "-" +
                           std::to_string(browser_id) + "-" +
                           std::to_string(++sequence_);
  if (!SharedMemory::Write(name, rgba, error)) {
    return false;
  }

  std::ostringstream control;
  control << "a=" << (extra_control.empty() ? "t" : "T")
          << ",f=32,t=s,s=" << width << ",v=" << height
          << ",S=" << rgba.size() << ",i=" << image_id << ",q=2";
  if (!extra_control.empty()) {
    control << ',' << extra_control;
  }
  const bool success = Write(GraphicsCommand(control.str(), Base64(name)), error);
  if (!success) {
    SharedMemory::Unlink(name);
  }
  return success;
}

bool KittyRenderer::UploadAnchor(std::uint32_t browser_id,
                                 std::uint32_t image_id,
                                 std::uint32_t placement_id,
                                 std::string* error) {
  const std::uint8_t transparent[] = {0, 0, 0, 0};
  std::ostringstream extra;
  extra << "U=1,p=" << placement_id << ",c=1,r=1,C=1";
  return Transmit(browser_id, image_id, 1, 1, transparent, extra.str(), error);
}

bool KittyRenderer::UploadRgba(std::uint32_t browser_id,
                               std::uint32_t image_id,
                               int width,
                               int height,
                               std::span<const std::uint8_t> rgba,
                               std::string* error) {
  return Transmit(browser_id, image_id, width, height, rgba, {}, error);
}

std::string KittyRenderer::FrameUpdateControl(std::uint32_t image_id,
                                              int x,
                                              int y,
                                              int width,
                                              int height,
                                              std::size_t bytes) {
  std::ostringstream control;
  control << "a=f,f=32,t=s,s=" << width << ",v=" << height << ",S="
          << bytes << ",i=" << image_id << ",q=2,x=" << x << ",y=" << y
          << ",r=1,X=1";
  return control.str();
}

bool KittyRenderer::UploadRgbaRegion(std::uint32_t browser_id,
                                     std::uint32_t image_id,
                                     int image_width,
                                     int image_height,
                                     int x,
                                     int y,
                                     int width,
                                     int height,
                                     std::span<const std::uint8_t> rgba,
                                     std::string* error) {
  std::size_t expected = 0;
  const bool inside = x >= 0 && y >= 0 && width >= 1 && height >= 1 &&
                      x <= image_width - width && y <= image_height - height;
  if (!inside || !CheckedRgbaSize(width, height, &expected) ||
      rgba.size() != expected) {
    if (error) {
      *error = "invalid RGBA update rectangle or byte count";
    }
    return false;
  }
  if (dry_run_) {
    return true;
  }
  return WriteSharedMemory(
      browser_id, rgba,
      FrameUpdateControl(image_id, x, y, width, height, rgba.size()), error);
}

bool KittyRenderer::WriteSharedMemory(std::uint32_t browser_id,
                                      std::span<const std::uint8_t> bytes,
                                      std::string control,
                                      std::string* error) {
  const std::string name = "/browser-" + std::to_string(getpid()) + "-" +
                           std::to_string(browser_id) + "-" +
                           std::to_string(++sequence_);
  if (!SharedMemory::Write(name, bytes, error)) {
    return false;
  }
  const bool success = Write(GraphicsCommand(control, Base64(name)), error);
  if (!success) {
    SharedMemory::Unlink(name);
  }
  return success;
}

bool KittyRenderer::PlaceRelative(std::uint32_t image_id,
                                  std::uint32_t placement_id,
                                  std::uint32_t parent_image_id,
                                  std::uint32_t parent_placement_id,
                                  int columns,
                                  int rows,
                                  std::string* error) {
  if (columns < 1 || rows < 1) {
    if (error) {
      *error = "relative placement must have positive rows and columns";
    }
    return false;
  }
  return Write(GraphicsCommand(RelativePlacementControl(
                   image_id, placement_id, parent_image_id,
                   parent_placement_id, columns, rows)),
               error);
}

std::string KittyRenderer::RelativePlacementControl(
    std::uint32_t image_id,
    std::uint32_t placement_id,
    std::uint32_t parent_image_id,
    std::uint32_t parent_placement_id,
    int columns,
    int rows) {
  std::ostringstream control;
  control << "a=p,i=" << image_id << ",p=" << placement_id
          << ",P=" << parent_image_id << ",Q=" << parent_placement_id
          << ",H=0,V=0,c=" << columns << ",r=" << rows
          << ",z=-1,C=1,q=2";
  return control.str();
}

bool KittyRenderer::DeletePlacement(std::uint32_t image_id,
                                    std::uint32_t placement_id,
                                    std::string* error) {
  const std::string control = "a=d,d=i,i=" + std::to_string(image_id) +
                              ",p=" + std::to_string(placement_id) + ",q=2";
  return Write(GraphicsCommand(control), error);
}

bool KittyRenderer::DeleteImage(std::uint32_t image_id, std::string* error) {
  const std::string control =
      "a=d,d=I,i=" + std::to_string(image_id) + ",q=2";
  return Write(GraphicsCommand(control), error);
}

}  // namespace browser
