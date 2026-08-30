#include "shm.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace browser {
namespace {

std::string Error(const char* operation) {
  return std::string(operation) + ": " + std::strerror(errno);
}

}  // namespace

bool SharedMemory::Write(const std::string& name,
                         std::span<const std::uint8_t> bytes,
                         std::string* error) {
  if (name.empty() || name.front() != '/') {
    if (error) {
      *error = "POSIX shared memory name must start with '/'";
    }
    return false;
  }
  if (bytes.empty()) {
    if (error) {
      *error = "cannot create an empty shared memory transfer";
    }
    return false;
  }

  shm_unlink(name.c_str());
  const int descriptor = shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
  if (descriptor < 0) {
    if (error) {
      *error = Error("shm_open");
    }
    return false;
  }

  bool success = true;
  if (ftruncate(descriptor, static_cast<off_t>(bytes.size())) != 0) {
    success = false;
    if (error) {
      *error = Error("ftruncate");
    }
  }

  void* mapping = MAP_FAILED;
  if (success) {
    mapping = mmap(nullptr, bytes.size(), PROT_READ | PROT_WRITE, MAP_SHARED,
                   descriptor, 0);
    if (mapping == MAP_FAILED) {
      success = false;
      if (error) {
        *error = Error("mmap");
      }
    }
  }

  if (success) {
    std::memcpy(mapping, bytes.data(), bytes.size());
  }
  if (mapping != MAP_FAILED) {
    munmap(mapping, bytes.size());
  }
  close(descriptor);

  if (!success) {
    shm_unlink(name.c_str());
  }
  return success;
}

void SharedMemory::Unlink(const std::string& name) {
  if (!name.empty()) {
    shm_unlink(name.c_str());
  }
}

}  // namespace browser
