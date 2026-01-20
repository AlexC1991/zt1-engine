#ifndef MEMORY_TRACKER_HPP
#define MEMORY_TRACKER_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <chrono>
#include <memory>

// ============================================================================
// MEMORY TRACKER - Diagnostic & Profiling System for ZT1-Engine
// ============================================================================
// Purpose: Track allocations/frees, attribute to systems, log discards
// Non-goals: NOT garbage collection, NOT memory safety enforcement
// ============================================================================

// Forward declaration
class MemoryTracker;

// ----------------------------------------------------------------------------
// Owner Tags - Identifies which system owns the memory
// ----------------------------------------------------------------------------
enum class MemoryOwner : uint8_t {
    Unknown = 0,
    World,
    ZooReader,
    SpriteDatabase,
    ResourceManager,
    Animation,
    FontManager,
    PalletManager,
    UiLayout,
    UiButton,
    UiImage,
    UiListBox,
    UiText,
    ScenarioManager,
    ScenarioDatabase,
    Config,
    ZtdFile,
    IniReader,
    AssetBuffer,
    Texture,          // SDL_Texture tracking
    Surface,          // SDL_Surface tracking
    Font,             // TTF_Font tracking
    Music,            // Mix_Music tracking
    Temporary,        // Short-lived allocations
    COUNT
};

// Convert owner to string for logging
const char* memoryOwnerToString(MemoryOwner owner);

// ----------------------------------------------------------------------------
// Allocation Record - Stored for each active allocation
// ----------------------------------------------------------------------------
struct AllocationRecord {
    void* address = nullptr;
    size_t size = 0;
    MemoryOwner owner = MemoryOwner::Unknown;
    const char* file = nullptr;
    int line = 0;
    uint64_t timestamp = 0;         // Allocation time (ms since tracker start)
    uint32_t sequence = 0;          // Allocation sequence number
    const char* tag = nullptr;      // Optional custom tag (e.g., "grass_texture")
};

// ----------------------------------------------------------------------------
// Garbage Record - Stored when memory is freed (for debugging early frees)
// ----------------------------------------------------------------------------
struct GarbageRecord {
    size_t size = 0;
    MemoryOwner owner = MemoryOwner::Unknown;
    const char* file = nullptr;
    int line = 0;
    const char* alloc_file = nullptr;   // Where it was allocated
    int alloc_line = 0;
    uint64_t alloc_time = 0;            // When allocated
    uint64_t free_time = 0;             // When freed
    uint64_t lifetime_ms = 0;           // How long it lived
    const char* tag = nullptr;
};

// ----------------------------------------------------------------------------
// System Statistics - Per-owner summary
// ----------------------------------------------------------------------------
struct SystemStats {
    size_t current_bytes = 0;
    size_t current_count = 0;
    size_t peak_bytes = 0;
    size_t peak_count = 0;
    size_t total_allocated = 0;
    size_t total_freed = 0;
    size_t alloc_count = 0;
    size_t free_count = 0;

    // Churn detection (allocs/frees in short time window)
    size_t recent_allocs = 0;
    size_t recent_frees = 0;
};

// ----------------------------------------------------------------------------
// Memory Tracker - Singleton diagnostic system
// ----------------------------------------------------------------------------
class MemoryTracker {
public:
    static MemoryTracker& get();

    // --- Core Tracking API ---

    // Record an allocation
    void recordAlloc(void* ptr, size_t size, MemoryOwner owner,
                     const char* file, int line, const char* tag = nullptr);

    // Record a free (moves to garbage log)
    void recordFree(void* ptr, const char* file, int line);

    // Record a realloc
    void recordRealloc(void* old_ptr, void* new_ptr, size_t new_size,
                       const char* file, int line);

    // --- SDL Resource Tracking (separate from malloc) ---
    void recordSDLAlloc(void* ptr, size_t estimated_size, MemoryOwner type,
                        const char* file, int line, const char* tag = nullptr);
    void recordSDLFree(void* ptr, const char* file, int line);

    // --- Queries ---

    // Get current allocation for a pointer (nullptr if not found)
    const AllocationRecord* getAllocation(void* ptr) const;

    // Check if pointer is tracked
    bool isTracked(void* ptr) const;

    // Get statistics for a specific system
    SystemStats getSystemStats(MemoryOwner owner) const;

    // Get total memory currently allocated
    size_t getTotalAllocated() const;

    // --- Garbage Log Queries ---

    // Get recent garbage records (last N frees)
    std::vector<GarbageRecord> getRecentGarbage(size_t count = 50) const;

    // Get garbage for specific system
    std::vector<GarbageRecord> getGarbageByOwner(MemoryOwner owner, size_t count = 50) const;

    // --- Suspicious Pattern Detection ---

    struct SuspiciousPattern {
        MemoryOwner owner;
        std::string description;
        size_t count;
        size_t bytes;
    };

    // Detect high churn (frequent alloc/free cycles)
    std::vector<SuspiciousPattern> detectHighChurn(float threshold_ratio = 2.0f) const;

    // Detect large unexpected frees
    std::vector<GarbageRecord> detectLargeFrees(size_t min_size = 1024 * 1024) const;

    // --- Reporting ---

    // Print current memory per system to SDL_Log
    void reportCurrentMemory() const;

    // Print recently freed memory to SDL_Log
    void reportRecentGarbage(size_t count = 20) const;

    // Print systems with high allocation churn
    void reportHighChurn() const;

    // Print full detailed report
    void reportFull() const;

    // --- Configuration ---

    // Set maximum garbage records to keep (default: 1000)
    void setGarbageHistorySize(size_t max_records);

    // Enable/disable tracking (for performance)
    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled_; }

    // Reset all tracking data
    void reset();

    // --- Context Stack (for attributing allocations) ---

    // Push/pop owner context for automatic attribution
    void pushContext(MemoryOwner owner, const char* tag = nullptr);
    void popContext();
    MemoryOwner getCurrentContext() const;
    const char* getCurrentTag() const;

private:
    MemoryTracker();
    ~MemoryTracker() = default;

    // Non-copyable
    MemoryTracker(const MemoryTracker&) = delete;
    MemoryTracker& operator=(const MemoryTracker&) = delete;

    uint64_t getTimestamp() const;
    void updateStats(MemoryOwner owner, size_t size, bool is_alloc);
    void addToGarbageLog(const AllocationRecord& record, const char* free_file, int free_line);

    // Data
    mutable std::mutex mutex_;
    std::unordered_map<void*, AllocationRecord> allocations_;
    std::vector<GarbageRecord> garbage_log_;
    SystemStats stats_[static_cast<size_t>(MemoryOwner::COUNT)];

    // Context stack for automatic owner attribution
    struct Context {
        MemoryOwner owner;
        const char* tag;
    };
    std::vector<Context> context_stack_;

    // Configuration
    size_t max_garbage_records_ = 1000;
    bool enabled_ = true;
    uint32_t next_sequence_ = 0;
    std::chrono::steady_clock::time_point start_time_;

    // Churn window tracking
    uint64_t churn_window_start_ = 0;
    static constexpr uint64_t CHURN_WINDOW_MS = 5000; // 5 second window
};

// ----------------------------------------------------------------------------
// RAII Context Guard - Automatically sets/restores tracking context
// ----------------------------------------------------------------------------
class MemoryContextGuard {
public:
    MemoryContextGuard(MemoryOwner owner, const char* tag = nullptr) {
        MemoryTracker::get().pushContext(owner, tag);
    }
    ~MemoryContextGuard() {
        MemoryTracker::get().popContext();
    }

    // Non-copyable, non-movable
    MemoryContextGuard(const MemoryContextGuard&) = delete;
    MemoryContextGuard& operator=(const MemoryContextGuard&) = delete;
};

// ----------------------------------------------------------------------------
// Tracked Allocator - STL-compatible allocator with tracking
// ----------------------------------------------------------------------------
template<typename T, MemoryOwner Owner = MemoryOwner::Unknown>
class TrackedAllocator {
public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    TrackedAllocator() noexcept = default;

    template<typename U>
    TrackedAllocator(const TrackedAllocator<U, Owner>&) noexcept {}

    T* allocate(size_type n) {
        size_t bytes = n * sizeof(T);
        T* ptr = static_cast<T*>(std::malloc(bytes));
        if (ptr) {
            MemoryTracker::get().recordAlloc(ptr, bytes, Owner, __FILE__, __LINE__);
        }
        return ptr;
    }

    void deallocate(T* ptr, size_type n) noexcept {
        if (ptr) {
            MemoryTracker::get().recordFree(ptr, __FILE__, __LINE__);
            std::free(ptr);
        }
    }

    template<typename U>
    bool operator==(const TrackedAllocator<U, Owner>&) const noexcept { return true; }

    template<typename U>
    bool operator!=(const TrackedAllocator<U, Owner>&) const noexcept { return false; }
};

// ----------------------------------------------------------------------------
// Convenience Macros - Use these for easy integration
// ----------------------------------------------------------------------------

// Enable/disable tracking at compile time
#ifndef ZT_MEMORY_TRACKING
#define ZT_MEMORY_TRACKING 1
#endif

#if ZT_MEMORY_TRACKING

// Tracked malloc/free replacements
#define ZT_MALLOC(size, owner) \
    zt_tracked_malloc(size, owner, __FILE__, __LINE__)

#define ZT_MALLOC_TAG(size, owner, tag) \
    zt_tracked_malloc_tag(size, owner, __FILE__, __LINE__, tag)

#define ZT_FREE(ptr) \
    zt_tracked_free(ptr, __FILE__, __LINE__)

#define ZT_REALLOC(ptr, size) \
    zt_tracked_realloc(ptr, size, __FILE__, __LINE__)

// Tracked new/delete (use sparingly, prefer context-based tracking)
#define ZT_NEW(type, owner, ...) \
    zt_tracked_new<type>(owner, __FILE__, __LINE__, ##__VA_ARGS__)

#define ZT_DELETE(ptr) \
    zt_tracked_delete(ptr, __FILE__, __LINE__)

// Context macros
#define ZT_MEMORY_CONTEXT(owner) \
    MemoryContextGuard _zt_mem_ctx_##__LINE__(owner)

#define ZT_MEMORY_CONTEXT_TAG(owner, tag) \
    MemoryContextGuard _zt_mem_ctx_##__LINE__(owner, tag)

// SDL resource tracking
#define ZT_SDL_ALLOC(ptr, size, type, tag) \
    MemoryTracker::get().recordSDLAlloc(ptr, size, type, __FILE__, __LINE__, tag)

#define ZT_SDL_FREE(ptr) \
    MemoryTracker::get().recordSDLFree(ptr, __FILE__, __LINE__)

// Reporting
#define ZT_MEMORY_REPORT() \
    MemoryTracker::get().reportCurrentMemory()

#define ZT_MEMORY_REPORT_FULL() \
    MemoryTracker::get().reportFull()

#define ZT_MEMORY_REPORT_GARBAGE(n) \
    MemoryTracker::get().reportRecentGarbage(n)

#else // ZT_MEMORY_TRACKING disabled

#define ZT_MALLOC(size, owner) std::malloc(size)
#define ZT_MALLOC_TAG(size, owner, tag) std::malloc(size)
#define ZT_FREE(ptr) std::free(ptr)
#define ZT_REALLOC(ptr, size) std::realloc(ptr, size)
#define ZT_NEW(type, owner, ...) new type(__VA_ARGS__)
#define ZT_DELETE(ptr) delete ptr
#define ZT_MEMORY_CONTEXT(owner) ((void)0)
#define ZT_MEMORY_CONTEXT_TAG(owner, tag) ((void)0)
#define ZT_SDL_ALLOC(ptr, size, type, tag) ((void)0)
#define ZT_SDL_FREE(ptr) ((void)0)
#define ZT_MEMORY_REPORT() ((void)0)
#define ZT_MEMORY_REPORT_FULL() ((void)0)
#define ZT_MEMORY_REPORT_GARBAGE(n) ((void)0)

#endif // ZT_MEMORY_TRACKING

// ----------------------------------------------------------------------------
// Helper Functions (Implementation in .cpp)
// ----------------------------------------------------------------------------

void* zt_tracked_malloc(size_t size, MemoryOwner owner, const char* file, int line);
void* zt_tracked_malloc_tag(size_t size, MemoryOwner owner, const char* file, int line, const char* tag);
void zt_tracked_free(void* ptr, const char* file, int line);
void* zt_tracked_realloc(void* ptr, size_t size, const char* file, int line);

template<typename T, typename... Args>
T* zt_tracked_new(MemoryOwner owner, const char* file, int line, Args&&... args) {
    void* mem = std::malloc(sizeof(T));
    if (mem) {
        MemoryTracker::get().recordAlloc(mem, sizeof(T), owner, file, line);
        return new(mem) T(std::forward<Args>(args)...);
    }
    return nullptr;
}

template<typename T>
void zt_tracked_delete(T* ptr, const char* file, int line) {
    if (ptr) {
        ptr->~T();
        MemoryTracker::get().recordFree(ptr, file, line);
        std::free(ptr);
    }
}

#endif // MEMORY_TRACKER_HPP
