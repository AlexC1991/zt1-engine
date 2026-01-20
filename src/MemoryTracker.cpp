#include "MemoryTracker.hpp"
#include <SDL.h>
#include <algorithm>
#include <cstring>

// ============================================================================
// Owner to String Conversion
// ============================================================================

const char* memoryOwnerToString(MemoryOwner owner) {
    switch (owner) {
        case MemoryOwner::Unknown:          return "Unknown";
        case MemoryOwner::World:            return "World";
        case MemoryOwner::ZooReader:        return "ZooReader";
        case MemoryOwner::SpriteDatabase:   return "SpriteDatabase";
        case MemoryOwner::ResourceManager:  return "ResourceManager";
        case MemoryOwner::Animation:        return "Animation";
        case MemoryOwner::FontManager:      return "FontManager";
        case MemoryOwner::PalletManager:    return "PalletManager";
        case MemoryOwner::UiLayout:         return "UiLayout";
        case MemoryOwner::UiButton:         return "UiButton";
        case MemoryOwner::UiImage:          return "UiImage";
        case MemoryOwner::UiListBox:        return "UiListBox";
        case MemoryOwner::UiText:           return "UiText";
        case MemoryOwner::ScenarioManager:  return "ScenarioManager";
        case MemoryOwner::ScenarioDatabase: return "ScenarioDatabase";
        case MemoryOwner::Config:           return "Config";
        case MemoryOwner::ZtdFile:          return "ZtdFile";
        case MemoryOwner::IniReader:        return "IniReader";
        case MemoryOwner::AssetBuffer:      return "AssetBuffer";
        case MemoryOwner::Texture:          return "Texture";
        case MemoryOwner::Surface:          return "Surface";
        case MemoryOwner::Font:             return "Font";
        case MemoryOwner::Music:            return "Music";
        case MemoryOwner::Temporary:        return "Temporary";
        default:                            return "Invalid";
    }
}

// ============================================================================
// MemoryTracker Implementation
// ============================================================================

MemoryTracker::MemoryTracker() {
    start_time_ = std::chrono::steady_clock::now();
    churn_window_start_ = 0;

    // Reserve space to avoid early reallocations
    allocations_.reserve(10000);
    garbage_log_.reserve(max_garbage_records_);

    // Initialize stats
    for (size_t i = 0; i < static_cast<size_t>(MemoryOwner::COUNT); ++i) {
        stats_[i] = SystemStats{};
    }
}

MemoryTracker& MemoryTracker::get() {
    static MemoryTracker instance;
    return instance;
}

uint64_t MemoryTracker::getTimestamp() const {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_);
    return static_cast<uint64_t>(duration.count());
}

// ============================================================================
// Core Tracking
// ============================================================================

void MemoryTracker::recordAlloc(void* ptr, size_t size, MemoryOwner owner,
                                 const char* file, int line, const char* tag) {
    if (!enabled_ || !ptr) return;

    std::lock_guard<std::mutex> lock(mutex_);

    // Use context if owner is Unknown
    if (owner == MemoryOwner::Unknown && !context_stack_.empty()) {
        owner = context_stack_.back().owner;
        if (!tag) tag = context_stack_.back().tag;
    }

    AllocationRecord record;
    record.address = ptr;
    record.size = size;
    record.owner = owner;
    record.file = file;
    record.line = line;
    record.timestamp = getTimestamp();
    record.sequence = next_sequence_++;
    record.tag = tag;

    allocations_[ptr] = record;
    updateStats(owner, size, true);
}

void MemoryTracker::recordFree(void* ptr, const char* file, int line) {
    if (!enabled_ || !ptr) return;

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = allocations_.find(ptr);
    if (it != allocations_.end()) {
        // Move to garbage log before removing
        addToGarbageLog(it->second, file, line);
        updateStats(it->second.owner, it->second.size, false);
        allocations_.erase(it);
    }
    // Note: Freeing untracked memory is silently ignored (common with external libs)
}

void MemoryTracker::recordRealloc(void* old_ptr, void* new_ptr, size_t new_size,
                                   const char* file, int line) {
    if (!enabled_) return;

    std::lock_guard<std::mutex> lock(mutex_);

    MemoryOwner owner = MemoryOwner::Unknown;
    const char* tag = nullptr;

    // Find old allocation info
    if (old_ptr) {
        auto it = allocations_.find(old_ptr);
        if (it != allocations_.end()) {
            owner = it->second.owner;
            tag = it->second.tag;
            addToGarbageLog(it->second, file, line);
            updateStats(it->second.owner, it->second.size, false);
            allocations_.erase(it);
        }
    }

    // Record new allocation
    if (new_ptr) {
        if (owner == MemoryOwner::Unknown && !context_stack_.empty()) {
            owner = context_stack_.back().owner;
            if (!tag) tag = context_stack_.back().tag;
        }

        AllocationRecord record;
        record.address = new_ptr;
        record.size = new_size;
        record.owner = owner;
        record.file = file;
        record.line = line;
        record.timestamp = getTimestamp();
        record.sequence = next_sequence_++;
        record.tag = tag;

        allocations_[new_ptr] = record;
        updateStats(owner, new_size, true);
    }
}

// ============================================================================
// SDL Resource Tracking
// ============================================================================

void MemoryTracker::recordSDLAlloc(void* ptr, size_t estimated_size, MemoryOwner type,
                                    const char* file, int line, const char* tag) {
    // SDL resources are tracked the same way, but with specific type owners
    recordAlloc(ptr, estimated_size, type, file, line, tag);
}

void MemoryTracker::recordSDLFree(void* ptr, const char* file, int line) {
    recordFree(ptr, file, line);
}

// ============================================================================
// Statistics
// ============================================================================

void MemoryTracker::updateStats(MemoryOwner owner, size_t size, bool is_alloc) {
    // Called under lock
    size_t idx = static_cast<size_t>(owner);
    if (idx >= static_cast<size_t>(MemoryOwner::COUNT)) return;

    SystemStats& s = stats_[idx];
    uint64_t now = getTimestamp();

    // Reset churn window if needed
    if (now - churn_window_start_ > CHURN_WINDOW_MS) {
        for (size_t i = 0; i < static_cast<size_t>(MemoryOwner::COUNT); ++i) {
            stats_[i].recent_allocs = 0;
            stats_[i].recent_frees = 0;
        }
        churn_window_start_ = now;
    }

    if (is_alloc) {
        s.current_bytes += size;
        s.current_count++;
        s.total_allocated += size;
        s.alloc_count++;
        s.recent_allocs++;

        if (s.current_bytes > s.peak_bytes) {
            s.peak_bytes = s.current_bytes;
        }
        if (s.current_count > s.peak_count) {
            s.peak_count = s.current_count;
        }
    } else {
        if (s.current_bytes >= size) {
            s.current_bytes -= size;
        }
        if (s.current_count > 0) {
            s.current_count--;
        }
        s.total_freed += size;
        s.free_count++;
        s.recent_frees++;
    }
}

void MemoryTracker::addToGarbageLog(const AllocationRecord& record,
                                     const char* free_file, int free_line) {
    // Called under lock
    GarbageRecord garbage;
    garbage.size = record.size;
    garbage.owner = record.owner;
    garbage.file = free_file;
    garbage.line = free_line;
    garbage.alloc_file = record.file;
    garbage.alloc_line = record.line;
    garbage.alloc_time = record.timestamp;
    garbage.free_time = getTimestamp();
    garbage.lifetime_ms = garbage.free_time - garbage.alloc_time;
    garbage.tag = record.tag;

    // Circular buffer behavior
    if (garbage_log_.size() >= max_garbage_records_) {
        garbage_log_.erase(garbage_log_.begin());
    }
    garbage_log_.push_back(garbage);
}

// ============================================================================
// Queries
// ============================================================================

const AllocationRecord* MemoryTracker::getAllocation(void* ptr) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = allocations_.find(ptr);
    if (it != allocations_.end()) {
        return &it->second;
    }
    return nullptr;
}

bool MemoryTracker::isTracked(void* ptr) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return allocations_.find(ptr) != allocations_.end();
}

SystemStats MemoryTracker::getSystemStats(MemoryOwner owner) const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t idx = static_cast<size_t>(owner);
    if (idx < static_cast<size_t>(MemoryOwner::COUNT)) {
        return stats_[idx];
    }
    return SystemStats{};
}

size_t MemoryTracker::getTotalAllocated() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t total = 0;
    for (size_t i = 0; i < static_cast<size_t>(MemoryOwner::COUNT); ++i) {
        total += stats_[i].current_bytes;
    }
    return total;
}

// ============================================================================
// Garbage Log Queries
// ============================================================================

std::vector<GarbageRecord> MemoryTracker::getRecentGarbage(size_t count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<GarbageRecord> result;

    size_t start = garbage_log_.size() > count ? garbage_log_.size() - count : 0;
    for (size_t i = start; i < garbage_log_.size(); ++i) {
        result.push_back(garbage_log_[i]);
    }
    return result;
}

std::vector<GarbageRecord> MemoryTracker::getGarbageByOwner(MemoryOwner owner, size_t count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<GarbageRecord> result;

    // Iterate backwards to get most recent first
    for (auto it = garbage_log_.rbegin(); it != garbage_log_.rend() && result.size() < count; ++it) {
        if (it->owner == owner) {
            result.push_back(*it);
        }
    }
    return result;
}

// ============================================================================
// Suspicious Pattern Detection
// ============================================================================

std::vector<MemoryTracker::SuspiciousPattern> MemoryTracker::detectHighChurn(float threshold_ratio) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SuspiciousPattern> patterns;

    for (size_t i = 0; i < static_cast<size_t>(MemoryOwner::COUNT); ++i) {
        const SystemStats& s = stats_[i];

        // High churn: many allocs AND many frees in the same window
        if (s.recent_allocs > 10 && s.recent_frees > 10) {
            float ratio = static_cast<float>(s.recent_allocs + s.recent_frees) /
                         static_cast<float>(std::max(s.current_count, size_t(1)));
            if (ratio > threshold_ratio) {
                SuspiciousPattern p;
                p.owner = static_cast<MemoryOwner>(i);
                p.description = "High allocation churn detected";
                p.count = s.recent_allocs + s.recent_frees;
                p.bytes = 0; // Could sum recent allocations
                patterns.push_back(p);
            }
        }
    }
    return patterns;
}

std::vector<GarbageRecord> MemoryTracker::detectLargeFrees(size_t min_size) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<GarbageRecord> large_frees;

    for (const auto& g : garbage_log_) {
        if (g.size >= min_size) {
            large_frees.push_back(g);
        }
    }
    return large_frees;
}

// ============================================================================
// Reporting
// ============================================================================

static const char* formatBytes(size_t bytes, char* buf, size_t buf_size) {
    if (bytes >= 1024 * 1024 * 1024) {
        snprintf(buf, buf_size, "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024 * 1024) {
        snprintf(buf, buf_size, "%.2f MB", bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024) {
        snprintf(buf, buf_size, "%.2f KB", bytes / 1024.0);
    } else {
        snprintf(buf, buf_size, "%zu B", bytes);
    }
    return buf;
}

void MemoryTracker::reportCurrentMemory() const {
    std::lock_guard<std::mutex> lock(mutex_);

    SDL_Log("=== MEMORY TRACKER: Current Allocations ===");
    SDL_Log("%-20s %12s %8s %12s %8s", "System", "Current", "Count", "Peak", "Peak Cnt");
    SDL_Log("------------------------------------------------------------");

    char buf1[32], buf2[32];
    size_t total_bytes = 0;
    size_t total_count = 0;

    for (size_t i = 0; i < static_cast<size_t>(MemoryOwner::COUNT); ++i) {
        const SystemStats& s = stats_[i];
        if (s.current_bytes > 0 || s.alloc_count > 0) {
            SDL_Log("%-20s %12s %8zu %12s %8zu",
                    memoryOwnerToString(static_cast<MemoryOwner>(i)),
                    formatBytes(s.current_bytes, buf1, sizeof(buf1)),
                    s.current_count,
                    formatBytes(s.peak_bytes, buf2, sizeof(buf2)),
                    s.peak_count);
            total_bytes += s.current_bytes;
            total_count += s.current_count;
        }
    }

    SDL_Log("------------------------------------------------------------");
    SDL_Log("%-20s %12s %8zu", "TOTAL",
            formatBytes(total_bytes, buf1, sizeof(buf1)), total_count);
    SDL_Log("============================================================");
}

void MemoryTracker::reportRecentGarbage(size_t count) const {
    std::lock_guard<std::mutex> lock(mutex_);

    SDL_Log("=== MEMORY TRACKER: Recent Frees (Garbage Log) ===");
    SDL_Log("%-20s %12s %10s %s", "System", "Size", "Lifetime", "Alloc Site");
    SDL_Log("------------------------------------------------------------");

    char buf[32];
    size_t start = garbage_log_.size() > count ? garbage_log_.size() - count : 0;

    for (size_t i = start; i < garbage_log_.size(); ++i) {
        const GarbageRecord& g = garbage_log_[i];

        // Extract filename from path
        const char* filename = g.alloc_file;
        if (filename) {
            const char* slash = strrchr(filename, '/');
            const char* backslash = strrchr(filename, '\\');
            if (backslash && (!slash || backslash > slash)) slash = backslash;
            if (slash) filename = slash + 1;
        } else {
            filename = "unknown";
        }

        SDL_Log("%-20s %12s %8llu ms %s:%d%s%s",
                memoryOwnerToString(g.owner),
                formatBytes(g.size, buf, sizeof(buf)),
                (unsigned long long)g.lifetime_ms,
                filename,
                g.alloc_line,
                g.tag ? " [" : "",
                g.tag ? g.tag : "");
        if (g.tag) SDL_Log("]");
    }

    SDL_Log("============================================================");
}

void MemoryTracker::reportHighChurn() const {
    SDL_Log("=== MEMORY TRACKER: High Churn Systems ===");

    auto patterns = detectHighChurn(2.0f);
    if (patterns.empty()) {
        SDL_Log("No high-churn systems detected.");
    } else {
        for (const auto& p : patterns) {
            SDL_Log("%-20s: %s (ops: %zu)",
                    memoryOwnerToString(p.owner),
                    p.description.c_str(),
                    p.count);
        }
    }

    SDL_Log("============================================================");
}

void MemoryTracker::reportFull() const {
    SDL_Log("");
    SDL_Log("##############################################################");
    SDL_Log("###           MEMORY TRACKER FULL REPORT                   ###");
    SDL_Log("##############################################################");
    SDL_Log("");

    reportCurrentMemory();
    SDL_Log("");
    reportHighChurn();
    SDL_Log("");
    reportRecentGarbage(30);

    // Large frees
    auto large = detectLargeFrees(512 * 1024); // 512KB+
    if (!large.empty()) {
        SDL_Log("");
        SDL_Log("=== Large Frees (512KB+) ===");
        char buf[32];
        for (const auto& g : large) {
            SDL_Log("%-20s: %s (lived %llu ms)",
                    memoryOwnerToString(g.owner),
                    formatBytes(g.size, buf, sizeof(buf)),
                    (unsigned long long)g.lifetime_ms);
        }
    }

    SDL_Log("");
    SDL_Log("##############################################################");
}

// ============================================================================
// Configuration
// ============================================================================

void MemoryTracker::setGarbageHistorySize(size_t max_records) {
    std::lock_guard<std::mutex> lock(mutex_);
    max_garbage_records_ = max_records;
    if (garbage_log_.size() > max_records) {
        garbage_log_.erase(garbage_log_.begin(),
                          garbage_log_.begin() + (garbage_log_.size() - max_records));
    }
}

void MemoryTracker::setEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    enabled_ = enabled;
}

void MemoryTracker::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    allocations_.clear();
    garbage_log_.clear();
    context_stack_.clear();
    for (size_t i = 0; i < static_cast<size_t>(MemoryOwner::COUNT); ++i) {
        stats_[i] = SystemStats{};
    }
    next_sequence_ = 0;
    start_time_ = std::chrono::steady_clock::now();
    churn_window_start_ = 0;
}

// ============================================================================
// Context Stack
// ============================================================================

void MemoryTracker::pushContext(MemoryOwner owner, const char* tag) {
    std::lock_guard<std::mutex> lock(mutex_);
    context_stack_.push_back({owner, tag});
}

void MemoryTracker::popContext() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!context_stack_.empty()) {
        context_stack_.pop_back();
    }
}

MemoryOwner MemoryTracker::getCurrentContext() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!context_stack_.empty()) {
        return context_stack_.back().owner;
    }
    return MemoryOwner::Unknown;
}

const char* MemoryTracker::getCurrentTag() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!context_stack_.empty()) {
        return context_stack_.back().tag;
    }
    return nullptr;
}

// ============================================================================
// Helper Functions
// ============================================================================

void* zt_tracked_malloc(size_t size, MemoryOwner owner, const char* file, int line) {
    void* ptr = std::malloc(size);
    if (ptr) {
        MemoryTracker::get().recordAlloc(ptr, size, owner, file, line);
    }
    return ptr;
}

void* zt_tracked_malloc_tag(size_t size, MemoryOwner owner, const char* file, int line, const char* tag) {
    void* ptr = std::malloc(size);
    if (ptr) {
        MemoryTracker::get().recordAlloc(ptr, size, owner, file, line, tag);
    }
    return ptr;
}

void zt_tracked_free(void* ptr, const char* file, int line) {
    if (ptr) {
        MemoryTracker::get().recordFree(ptr, file, line);
        std::free(ptr);
    }
}

void* zt_tracked_realloc(void* ptr, size_t size, const char* file, int line) {
    void* new_ptr = std::realloc(ptr, size);
    MemoryTracker::get().recordRealloc(ptr, new_ptr, size, file, line);
    return new_ptr;
}
