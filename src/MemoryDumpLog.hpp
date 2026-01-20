#ifndef MEMORY_DUMP_LOG_HPP
#define MEMORY_DUMP_LOG_HPP

#include "MemoryTracker.hpp"
#include <string>
#include <fstream>
#include <chrono>
#include <ctime>

// ============================================================================
// MEMORY DUMP LOG - File-based logging for crash analysis
// ============================================================================
// Writes memory state to timestamped .txt files:
// - On manual request
// - On periodic intervals
// - On crash/exit (via atexit handler)
// ============================================================================

class MemoryDumpLog {
public:
    static MemoryDumpLog& get();

    // --- Configuration ---

    // Set output directory (default: "logs/")
    void setOutputDirectory(const std::string& dir);

    // Set auto-dump interval in seconds (0 = disabled)
    void setAutoDumpInterval(int seconds);

    // Enable/disable crash handler registration
    void enableCrashHandler(bool enable = true);

    // --- Manual Dump ---

    // Write full memory report to file, returns filename
    std::string dumpToFile(const std::string& reason = "manual");

    // Write just garbage log to file
    std::string dumpGarbageLog(const std::string& reason = "garbage");

    // --- Periodic Dump ---

    // Call this periodically (e.g., in main loop) to check if auto-dump needed
    void checkAutoDump();

    // --- Lifecycle ---

    // Initialize (registers atexit handler if enabled)
    void initialize();

    // Shutdown (writes final dump)
    void shutdown();

    // Get last dump filename
    const std::string& getLastDumpFile() const { return last_dump_file_; }

private:
    MemoryDumpLog();
    ~MemoryDumpLog();

    // Non-copyable
    MemoryDumpLog(const MemoryDumpLog&) = delete;
    MemoryDumpLog& operator=(const MemoryDumpLog&) = delete;

    std::string generateFilename(const std::string& reason) const;
    void writeHeader(std::ofstream& file, const std::string& reason) const;
    void writeCurrentMemory(std::ofstream& file) const;
    void writeGarbageLog(std::ofstream& file, size_t count) const;
    void writeChurnAnalysis(std::ofstream& file) const;
    void writeLargeFrees(std::ofstream& file) const;
    void writeAllActiveAllocations(std::ofstream& file) const;

    static void atExitHandler();

    std::string output_dir_ = "logs";
    std::string last_dump_file_;
    int auto_dump_interval_seconds_ = 0;
    std::chrono::steady_clock::time_point last_auto_dump_;
    bool crash_handler_enabled_ = true;
    bool initialized_ = false;
};

// ============================================================================
// Convenience Macros
// ============================================================================

#if ZT_MEMORY_TRACKING

#define ZT_MEMORY_DUMP(reason) \
    MemoryDumpLog::get().dumpToFile(reason)

#define ZT_MEMORY_DUMP_INIT() \
    MemoryDumpLog::get().initialize()

#define ZT_MEMORY_DUMP_SHUTDOWN() \
    MemoryDumpLog::get().shutdown()

#define ZT_MEMORY_DUMP_CHECK() \
    MemoryDumpLog::get().checkAutoDump()

#else

#define ZT_MEMORY_DUMP(reason) ((void)0)
#define ZT_MEMORY_DUMP_INIT() ((void)0)
#define ZT_MEMORY_DUMP_SHUTDOWN() ((void)0)
#define ZT_MEMORY_DUMP_CHECK() ((void)0)

#endif

#endif // MEMORY_DUMP_LOG_HPP
