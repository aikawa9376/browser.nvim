#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace browser {

class SharedMemory {
 public:
  static bool Write(const std::string& name,
                    std::span<const std::uint8_t> bytes,
                    std::string* error);
  static void Unlink(const std::string& name);
};

}  // namespace browser
