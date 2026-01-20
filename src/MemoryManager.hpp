#ifndef MEMORY_MANAGER_HPP
#define MEMORY_MANAGER_HPP

#include <SDL2/SDL.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>


// Simple buffer wrapper
struct AssetBuffer {
  void *data = nullptr;
  size_t size = 0;

  // Ownership flag (if true, destructor frees)
  bool owns = false;

  AssetBuffer() = default;

  // Move constructor
  AssetBuffer(AssetBuffer &&other) noexcept {
    data = other.data;
    size = other.size;
    owns = other.owns;
    other.data = nullptr;
    other.size = 0;
    other.owns = false;
  }

  // No copying
  AssetBuffer(const AssetBuffer &) = delete;
  AssetBuffer &operator=(const AssetBuffer &) = delete;

  ~AssetBuffer();

  void free();

  // Protection safe-guards
  bool checkBounds(size_t offset, size_t readSize) const {
    if (!data) {
      SDL_Log("MemoryManager: checkBounds failed - data is null");
      return false;
    }
    if (offset + readSize > size) {
      SDL_Log("MemoryManager: checkBounds failed - Overflow (Offset: %zu, "
              "Size: %zu, Buffer: %zu)",
              offset, readSize, size);
      return false;
    }
    return true;
  }

  template <typename T> bool safeRead(size_t offset, T *out) const {
    if (!checkBounds(offset, sizeof(T)) || !out)
      return false;
    // memcpy handles alignment safely
    memcpy(out, (uint8_t *)data + offset, sizeof(T));
    return true;
  }
};

class MemoryManager {
public:
  static MemoryManager &get();

  // Allocate generic buffer
  AssetBuffer allocate(size_t size);

  // Wrap existing pointer (transfer ownership)
  AssetBuffer wrap(void *ptr, size_t size);

private:
  MemoryManager() = default;
};

#endif // MEMORY_MANAGER_HPP
