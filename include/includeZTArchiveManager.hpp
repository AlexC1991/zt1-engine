#ifndef ZT_ARCHIVE_MANAGER_HPP
#define ZT_ARCHIVE_MANAGER_HPP

#include <string>
#include <vector>
#include <map>
#include <memory>

/**
 * @brief Manages .ZTD and .ZUP archives based on documented priorities.
 * See: docs/RESOURCE_ARCHITECTURE.md
 */
class ZTArchiveManager {
public:
    static ZTArchiveManager& get();

    // Initializes and mounts archives in the Release folder
    bool init(const std::string& releasePath);

    // Retrieves raw file data from the highest priority archive
    // Returns a pointer to the data and sets size
    unsigned char* getFile(const std::string& internalPath, size_t* size);

    // Checks if a file exists in any mounted archive
    bool fileExists(const std::string& internalPath);

private:
    ZTArchiveManager() = default;
    ~ZTArchiveManager();

    struct Archive {
        std::string path;
        int priority; // Documented: .zup > .ztd
        // Handle to zip/ztd archive would go here
    };

    std::vector<Archive> mountedArchives;
    
    // Internal method to sort archives by priority
    void sortArchives();
};

#endif // ZT_ARCHIVE_MANAGER_HPP