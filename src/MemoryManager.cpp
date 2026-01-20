#include "MemoryManager.hpp"
#include <cstdlib>
#include <SDL.h>

AssetBuffer::~AssetBuffer() {
    free();
}

void AssetBuffer::free() {
    if (owns && data) {
        std::free(data);
        data = nullptr;
        size = 0;
        owns = false;
    }
}

MemoryManager& MemoryManager::get() {
    static MemoryManager instance;
    return instance;
}

AssetBuffer MemoryManager::allocate(size_t size) {
    AssetBuffer buf;
    buf.data = std::malloc(size);
    buf.size = size;
    buf.owns = true;
    
    if (!buf.data && size > 0) {
        SDL_Log("MemoryManager: Failed to allocate %zu bytes", size);
    }
    
    return buf;
}

AssetBuffer MemoryManager::wrap(void* ptr, size_t size) {
    AssetBuffer buf;
    buf.data = ptr;
    buf.size = size;
    buf.owns = true; // Assume ownership
    return buf;
}
