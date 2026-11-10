#pragma once
// Per-store lock (SRS section 2.4; SAD ADR-002): an exclusive
// operating-system lock on "<canonical store path>.lock", so a second
// instance of the application can never write to the same data store.
// LockFileEx on Windows, flock on Linux. The OS releases the lock when the
// process ends, so a crash leaves no stale lock.
//
// Current scope: canonical path via std::filesystem::weakly_canonical, and the
// store and lock files are limited to the current user on both platforms.
// Not yet done (logged as deviations): hard-link refusal, network-location
// refusal.

#include <memory>
#include <string>

namespace hms {

class StoreLock {
public:
    ~StoreLock();
    StoreLock(const StoreLock&) = delete;
    StoreLock& operator=(const StoreLock&) = delete;

    // Returns nullptr and fills `error` if the lock is held by another
    // process or the lock file cannot be created.
    static std::unique_ptr<StoreLock> acquire(const std::string& canonicalStorePath, std::string& error);

    const std::string& lockPath() const { return lockPath_; }

private:
    StoreLock() = default;
    std::string lockPath_;
#ifdef _WIN32
    void* handle_ = nullptr;
#else
    int fd_ = -1;
#endif
};

// Absolute, symlink-resolved form of a store path (UTF-8 in, UTF-8 out).
std::string canonicalStorePath(const std::string& utf8Path);

// Creates an empty file readable and writable by the owner only (POSIX mode
// 0600; on Windows a protected ACL that names only the current user), created
// exclusively. Returns false if it already exists.
bool createOwnerOnlyFile(const std::string& utf8Path, std::string& error);

// Tightens an existing file (or folder) to the current user: mode 0600 (0700 for a folder) on
// POSIX, a protected ACL with the current user as the only entry on Windows (HMS-SR-005, D-07).
// A folder's entry is inherited by files created in it afterwards. Returns false and fills
// `error` if the permissions cannot be changed.
bool restrictToCurrentUser(const std::string& utf8Path, bool isFolder, std::string& error);

}  // namespace hms
