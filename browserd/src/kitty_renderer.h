#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace browser {

struct TerminalMetrics {
  int cell_width = 10;
  int cell_height = 20;
  int columns = 0;
  int rows = 0;
  int pixel_width = 0;
  int pixel_height = 0;
  std::string source = "fallback";
};

class KittyRenderer {
 public:
  explicit KittyRenderer(bool dry_run = false);
  ~KittyRenderer();

  KittyRenderer(const KittyRenderer&) = delete;
  KittyRenderer& operator=(const KittyRenderer&) = delete;

  [[nodiscard]] bool ready() const { return dry_run_ || tty_fd_ >= 0; }
  [[nodiscard]] const std::string& error() const { return error_; }
  [[nodiscard]] const TerminalMetrics& metrics() const { return metrics_; }

  bool UploadAnchor(std::uint32_t browser_id,
                    std::uint32_t image_id,
                    std::uint32_t placement_id,
                    std::string* error);
  bool UploadRgba(std::uint32_t browser_id,
                  std::uint32_t image_id,
                  int width,
                  int height,
                  std::span<const std::uint8_t> rgba,
                  std::string* error);
  bool UploadRgbaRegion(std::uint32_t browser_id,
                        std::uint32_t image_id,
                        int image_width,
                        int image_height,
                        int x,
                        int y,
                        int width,
                        int height,
                        std::span<const std::uint8_t> rgba,
                        std::string* error);
  bool PlaceRelative(std::uint32_t image_id,
                     std::uint32_t placement_id,
                     std::uint32_t parent_image_id,
                     std::uint32_t parent_placement_id,
                     int columns,
                     int rows,
                     std::string* error);
  bool DeletePlacement(std::uint32_t image_id,
                       std::uint32_t placement_id,
                       std::string* error);
  bool DeleteImage(std::uint32_t image_id, std::string* error);

  static std::string Base64(std::string_view input);
  static std::string GraphicsCommand(std::string_view control,
                                     std::string_view payload = {});
  static std::string WrapForTmux(std::string_view command);
  static std::string FrameUpdateControl(std::uint32_t image_id,
                                        int x,
                                        int y,
                                        int width,
                                        int height,
                                        std::size_t bytes);
  static std::string RelativePlacementControl(
      std::uint32_t image_id,
      std::uint32_t placement_id,
      std::uint32_t parent_image_id,
      std::uint32_t parent_placement_id,
      int columns,
      int rows);
  static std::vector<std::uint8_t> Gradient(int width, int height);

 private:
  bool Transmit(std::uint32_t browser_id,
                std::uint32_t image_id,
                int width,
                int height,
                std::span<const std::uint8_t> rgba,
                std::string_view extra_control,
                std::string* error);
  bool Write(std::string command, std::string* error);
  bool WriteSharedMemory(std::uint32_t browser_id,
                         std::span<const std::uint8_t> bytes,
                         std::string control,
                         std::string* error);
  void DetectMetrics();

  bool dry_run_ = false;
  bool in_tmux_ = false;
  int tty_fd_ = -1;
  std::uint64_t sequence_ = 0;
  std::string error_;
  TerminalMetrics metrics_;
};

}  // namespace browser
