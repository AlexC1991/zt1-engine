#ifndef MEMORY_MANAGER_HPP
#define MEMORY_MANAGER_HPP

#include <vector>
#include <cstdint>
#include <memory>
#include <optional>

// Simple buffer wrapper
struct AssetBuffer {
    void* data = nullptr;
    size_t size = 0;
    
    // Ownership flag (if true, destructor frees)
    bool owns = false;

    AssetBuffer() = default;
    
    // Move constructor
    AssetBuffer(AssetBuffer&& other) noexcept {
        data = other.data;
        size = other.size;
        owns = other.owns;
        other.data = nullptr;
        other.size = 0;
        other.owns = false;
    }
    
    // No copying
    AssetBuffer(const AssetBuffer&) = delete;
    AssetBuffer& operator=(const AssetBuffer&) = delete;

    ~AssetBuffer();
    
    void free();
};

class MemoryManager {
public:
    static MemoryManager& get();

    // Allocate generic buffer
    AssetBuffer allocate(size_t size);
    
    // Wrap existing pointer (transfer ownership)
    AssetBuffer wrap(void* ptr, size_t size);

private:
    MemoryManager() = default;
};

#endif // MEMORY_MANAGER_HPP
