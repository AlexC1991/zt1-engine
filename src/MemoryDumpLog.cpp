#include "MemoryDumpLog.hpp"
#include <SDL.h>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

// ============================================================================
// MemoryDumpLog Implementation
// ============================================================================

MemoryDumpLog::MemoryDumpLog() {
    last_auto_dump_ = std::chrono::steady_clock::now();
}

MemoryDumpLog::~MemoryDumpLog() {
    // Don't dump here - atexit handler handles it
}

MemoryDumpLog& MemoryDumpLog::get() {
    static MemoryDumpLog instance;
    return instance;
}

void MemoryDumpLog::atExitHandler() {
    // Called on normal exit or crash
    SDL_Log("MemoryDumpLog: Writing exit dump...");
    get().dumpToFile("exit");
}

void MemoryDumpLog::initialize() {
    if (initialized_) return;

    // Create logs directory if needed
    try {
        std::filesystem::create_directories(output_dir_);
    } catch (const std::exception& e) {
        SDL_Log("MemoryDumpLog: Could not create directory '%s': %s",
                output_dir_.c_str(), e.what());
    }

    // Register atexit handler
    if (crash_handler_enabled_) {
        std::atexit(atExitHandler);
    }

#ifdef _WIN32
    // Windows: Set up unhandled exception filter for crash dumps
    if (crash_handler_enabled_) {
        SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* exceptionInfo) -> LONG {
            SDL_Log("MemoryDumpLog: CRASH DETECTED - Writing crash dump...");
            get().dumpToFile("CRASH");
            return EXCEPTION_CONTINUE_SEARCH;
        });
    }
#endif

    initialized_ = true;
    SDL_Log("MemoryDumpLog: Initialized (output: %s/)", output_dir_.c_str());
}

void MemoryDumpLog::shutdown() {
    if (!initialized_) return;

    dumpToFile("shutdown");
    SDL_Log("MemoryDumpLog: Shutdown complete, dump written to: %s", last_dump_file_.c_str());
}

void MemoryDumpLog::setOutputDirectory(const std::string& dir) {
    output_dir_ = dir;
    try {
        std::filesystem::create_directories(output_dir_);
    } catch (const std::exception& e) {
        SDL_Log("MemoryDumpLog: Could not create directory '%s': %s",
                output_dir_.c_str(), e.what());
    }
}

void MemoryDumpLog::setAutoDumpInterval(int seconds) {
    auto_dump_interval_seconds_ = seconds;
    last_auto_dump_ = std::chrono::steady_clock::now();
}

void MemoryDumpLog::enableCrashHandler(bool enable) {
    crash_handler_enabled_ = enable;
}

void MemoryDumpLog::checkAutoDump() {
    if (auto_dump_interval_seconds_ <= 0) return;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_auto_dump_);

    if (elapsed.count() >= auto_dump_interval_seconds_) {
        dumpToFile("periodic");
        last_auto_dump_ = now;
    }
}

std::string MemoryDumpLog::generateFilename(const std::string& reason) const {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;

#ifdef _WIN32
    localtime_s(&tm_now, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_now);
#endif

    std::ostringstream oss;
    oss << output_dir_ << "/memory_"
        << std::put_time(&tm_now, "%Y%m%d_%H%M%S")
        << "_" << reason << ".txt";

    return oss.str();
}

static std::string formatBytes(size_t bytes) {
    std::ostringstream oss;
    if (bytes >= 1024 * 1024 * 1024) {
        oss << std::fixed << std::setprecision(2) << (bytes / (1024.0 * 1024.0 * 1024.0)) << " GB";
    } else if (bytes >= 1024 * 1024) {
        oss << std::fixed << std::setprecision(2) << (bytes / (1024.0 * 1024.0)) << " MB";
    } else if (bytes >= 1024) {
        oss << std::fixed << std::setprecision(2) << (bytes / 1024.0) << " KB";
    } else {
        oss << bytes << " B";
    }
    return oss.str();
}

static std::string extractFilename(const char* path) {
    if (!path) return "unknown";
    const char* slash = strrchr(path, '/');
    const char* backslash = strrchr(path, '\\');
    if (backslash && (!slash || backslash > slash)) slash = backslash;
    return slash ? slash + 1 : path;
}

std::string MemoryDumpLog::dumpToFile(const std::string& reason) {
    std::string filename = generateFilename(reason);
    std::ofstream file(filename);

    if (!file.is_open()) {
        SDL_Log("MemoryDumpLog: Failed to open file: %s", filename.c_str());
        return "";
    }

    writeHeader(file, reason);
    file << "\n";
    writeCurrentMemory(file);
    file << "\n";
    writeChurnAnalysis(file);
    file << "\n";
    writeLargeFrees(file);
    file << "\n";
    writeGarbageLog(file, 100);
    file << "\n";
    writeAllActiveAllocations(file);

    file.close();
    last_dump_file_ = filename;

    SDL_Log("MemoryDumpLog: Dump written to: %s", filename.c_str());
    return filename;
}

std::string MemoryDumpLog::dumpGarbageLog(const std::string& reason) {
    std::string filename = generateFilename(reason);
    std::ofstream file(filename);

    if (!file.is_open()) {
        SDL_Log("MemoryDumpLog: Failed to open file: %s", filename.c_str());
        return "";
    }

    writeHeader(file, reason);
    file << "\n";
    writeGarbageLog(file, 500);

    file.close();
    last_dump_file_ = filename;

    SDL_Log("MemoryDumpLog: Garbage log written to: %s", filename.c_str());
    return filename;
}

void MemoryDumpLog::writeHeader(std::ofstream& file, const std::string& reason) const {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;

#ifdef _WIN32
    localtime_s(&tm_now, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_now);
#endif

    file << "===============================================================================\n";
    file << "                      ZT1-ENGINE MEMORY DUMP REPORT\n";
    file << "===============================================================================\n";
    file << "Timestamp:  " << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S") << "\n";
    file << "Reason:     " << reason << "\n";
    file << "Total Mem:  " << formatBytes(MemoryTracker::get().getTotalAllocated()) << "\n";
    file << "===============================================================================\n";
}

void MemoryDumpLog::writeCurrentMemory(std::ofstream& file) const {
    file << "=== CURRENT MEMORY BY SYSTEM ===\n";
    file << std::left << std::setw(22) << "System"
         << std::right << std::setw(14) << "Current"
         << std::setw(10) << "Count"
         << std::setw(14) << "Peak"
         << std::setw(10) << "Peak Cnt"
         << std::setw(12) << "Allocs"
         << std::setw(12) << "Frees"
         << "\n";
    file << std::string(94, '-') << "\n";

    size_t total_bytes = 0;
    size_t total_count = 0;
    size_t total_allocs = 0;
    size_t total_frees = 0;

    for (size_t i = 0; i < static_cast<size_t>(MemoryOwner::COUNT); ++i) {
        SystemStats s = MemoryTracker::get().getSystemStats(static_cast<MemoryOwner>(i));
        if (s.current_bytes > 0 || s.alloc_count > 0) {
            file << std::left << std::setw(22) << memoryOwnerToString(static_cast<MemoryOwner>(i))
                 << std::right << std::setw(14) << formatBytes(s.current_bytes)
                 << std::setw(10) << s.current_count
                 << std::setw(14) << formatBytes(s.peak_bytes)
                 << std::setw(10) << s.peak_count
                 << std::setw(12) << s.alloc_count
                 << std::setw(12) << s.free_count
                 << "\n";

            total_bytes += s.current_bytes;
            total_count += s.current_count;
            total_allocs += s.alloc_count;
            total_frees += s.free_count;
        }
    }

    file << std::string(94, '-') << "\n";
    file << std::left << std::setw(22) << "TOTAL"
         << std::right << std::setw(14) << formatBytes(total_bytes)
         << std::setw(10) << total_count
         << std::setw(14) << ""
         << std::setw(10) << ""
         << std::setw(12) << total_allocs
         << std::setw(12) << total_frees
         << "\n";
}

void MemoryDumpLog::writeGarbageLog(std::ofstream& file, size_t count) const {
    file << "=== GARBAGE LOG (Recent Frees) ===\n";
    file << "Showing last " << count << " freed allocations:\n\n";

    file << std::left << std::setw(18) << "System"
         << std::right << std::setw(12) << "Size"
         << std::setw(12) << "Lifetime"
         << std::setw(30) << "Alloc Site"
         << std::setw(30) << "Free Site"
         << "  Tag\n";
    file << std::string(110, '-') << "\n";

    auto garbage = MemoryTracker::get().getRecentGarbage(count);

    for (const auto& g : garbage) {
        std::string alloc_site = extractFilename(g.alloc_file) + ":" + std::to_string(g.alloc_line);
        std::string free_site = extractFilename(g.file) + ":" + std::to_string(g.line);

        file << std::left << std::setw(18) << memoryOwnerToString(g.owner)
             << std::right << std::setw(12) << formatBytes(g.size)
             << std::setw(10) << g.lifetime_ms << " ms"
             << std::setw(30) << alloc_site
             << std::setw(30) << free_site
             << "  " << (g.tag ? g.tag : "") << "\n";
    }

    file << std::string(110, '-') << "\n";
    file << "Total entries: " << garbage.size() << "\n";
}

void MemoryDumpLog::writeChurnAnalysis(std::ofstream& file) const {
    file << "=== CHURN ANALYSIS ===\n";
    file << "Systems with high allocation churn (many alloc/free cycles):\n\n";

    auto patterns = MemoryTracker::get().detectHighChurn(1.5f);

    if (patterns.empty()) {
        file << "No high-churn patterns detected.\n";
    } else {
        for (const auto& p : patterns) {
            file << "  " << memoryOwnerToString(p.owner) << ": "
                 << p.description << " (operations: " << p.count << ")\n";
        }
    }
}

void MemoryDumpLog::writeLargeFrees(std::ofstream& file) const {
    file << "=== LARGE FREES (512KB+) ===\n";
    file << "Large memory blocks that were freed:\n\n";

    auto large = MemoryTracker::get().detectLargeFrees(512 * 1024);

    if (large.empty()) {
        file << "No large frees detected.\n";
    } else {
        file << std::left << std::setw(18) << "System"
             << std::right << std::setw(14) << "Size"
             << std::setw(12) << "Lifetime"
             << std::setw(35) << "Alloc Site"
             << "  Tag\n";
        file << std::string(85, '-') << "\n";

        for (const auto& g : large) {
            std::string site = extractFilename(g.alloc_file) + ":" + std::to_string(g.alloc_line);

            file << std::left << std::setw(18) << memoryOwnerToString(g.owner)
                 << std::right << std::setw(14) << formatBytes(g.size)
                 << std::setw(10) << g.lifetime_ms << " ms"
                 << std::setw(35) << site
                 << "  " << (g.tag ? g.tag : "") << "\n";
        }
    }
}

void MemoryDumpLog::writeAllActiveAllocations(std::ofstream& file) const {
    file << "=== ACTIVE ALLOCATIONS (Snapshot) ===\n";
    file << "All currently tracked memory allocations:\n\n";

    // Group allocations by owner for readability
    std::unordered_map<MemoryOwner, std::vector<const AllocationRecord*>> by_owner;

    // We need to access tracker internals - use getSystemStats to check which owners have allocations
    for (size_t i = 0; i < static_cast<size_t>(MemoryOwner::COUNT); ++i) {
        SystemStats s = MemoryTracker::get().getSystemStats(static_cast<MemoryOwner>(i));
        if (s.current_count > 0) {
            file << "\n--- " << memoryOwnerToString(static_cast<MemoryOwner>(i))
                 << " (" << s.current_count << " allocations, "
                 << formatBytes(s.current_bytes) << ") ---\n";

            // Note: We can't iterate allocations directly from outside.
            // This section shows summary by owner instead.
            file << "  Current: " << formatBytes(s.current_bytes) << " in " << s.current_count << " allocations\n";
            file << "  Peak:    " << formatBytes(s.peak_bytes) << " in " << s.peak_count << " allocations\n";
            file << "  Lifetime allocs: " << s.alloc_count << ", frees: " << s.free_count << "\n";
        }
    }

    file << "\n";
    file << "(Note: Individual allocation details require internal tracker access.\n";
    file << " See garbage log for detailed free records.)\n";
}
